import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const support = path.dirname(fileURLToPath(import.meta.url));
const native = path.resolve(support, '../engine/native-game');
const hash = (file) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

/** Real native interpreter/renderer acceptance; this does not emulate a C64 or assert physical hardware behavior. */
export function runMpe4SessionArcada({ raw, outDir, compiler = process.env.MPE4_CXX ?? process.env.CXX ?? (process.platform === 'win32' ? 'C:/msys64/mingw64/bin/g++.exe' : 'g++') }) {
  if (!raw || !outDir) throw new Error('raw and outDir are required');
  raw = path.resolve(raw);
  outDir = path.resolve(outDir);
  fs.mkdirSync(outDir, { recursive: true });
  const sources = [path.join(support, 'mpe4-session-arcada-harness.cpp'),
    ...['mpe4_game', 'mpe4_package', 'mpe4_session', 'mpe4_render'].map((name) => path.join(native, `${name}.cpp`))];
  const evidence = [...sources, ...['mpe4_game', 'mpe4_package', 'mpe4_session', 'mpe4_render'].map((name) => path.join(native, `${name}.h`))];
  const sourceHashes = evidence.map((file) => ({ file, sha256: hash(file) }));
  const rawHash = hash(raw);
  const executable = path.join(outDir, process.platform === 'win32' ? 'mpe4-session-arcada.exe' : 'mpe4-session-arcada');
  const args = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-misleading-indentation',
    ...(process.platform === 'win32' ? ['-static', '-static-libgcc', '-static-libstdc++'] : []),
    '-I', native, ...sources, '-o', executable];
  const build = spawnSync(compiler, args, { cwd: path.isAbsolute(compiler) ? path.dirname(compiler) : native,
    windowsHide: true, encoding: 'utf8', timeout: 60000 });
  fs.writeFileSync(path.join(outDir, 'session-arcada-build.log'), (build.stdout ?? '') + (build.stderr ?? ''));
  if (build.status !== 0) throw new Error(build.stderr || build.error?.message || 'Native Arcada harness compilation failed');
  const run = spawnSync(executable, [raw], { cwd: outDir, windowsHide: true, encoding: 'utf8', timeout: 60000 });
  fs.writeFileSync(path.join(outDir, 'session-arcada.log'), run.stderr ?? '');
  let result;
  try { result = JSON.parse(run.stdout); }
  catch { throw new Error(run.stderr || run.stdout || run.error?.message || 'Native Arcada harness produced no report'); }
  result.raw = { file: raw, sha256: rawHash, unchangedDuringRun: hash(raw) === rawHash };
  result.executable = { file: executable, sha256: hash(executable) };
  result.sourceHashes = sourceHashes.map((item) => ({ ...item, unchangedDuringBuild: hash(item.file) === item.sha256 }));
  result.executedAt = new Date().toISOString();
  result.passed &&= run.status === 0 && result.raw.unchangedDuringRun && result.sourceHashes.every((item) => item.unchangedDuringBuild);
  result.report = path.join(outDir, 'session-arcada-result.json');
  fs.writeFileSync(result.report, `${JSON.stringify(result, null, 2)}\n`);
  return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const options = {};
  for (let index = 2; index < process.argv.length; index += 2) {
    const key = { '--raw': 'raw', '--out': 'outDir', '--compiler': 'compiler' }[process.argv[index]];
    if (!key || !process.argv[index + 1]) throw new Error('Usage: node tests/run-mpe4-session-arcada.mjs --raw cartridge.bin --out output-directory [--compiler g++]');
    options[key] = process.argv[index + 1];
  }
  const result = runMpe4SessionArcada(options);
  console.log(JSON.stringify(result, null, 2));
  if (!result.passed) process.exitCode = 1;
}
