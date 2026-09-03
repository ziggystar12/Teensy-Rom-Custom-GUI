import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const tests = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(tests);
const source = path.join(tests, 'mhs-native-arena-owner-harness.cpp');
const header = path.join(root, 'engine', 'native-runtime', 'mhs_native_arena.h');
let out = path.join(root, 'build', 'mhs-native-arena-owner');
let compiler = process.env.MHS_NATIVE_CXX ?? process.env.CXX ??
  (process.platform === 'win32' ? 'C:/msys64/mingw64/bin/g++.exe' : 'g++');

for (let index = 2; index < process.argv.length; ++index) {
  if (process.argv[index] === '--out') out = path.resolve(process.argv[++index]);
  else if (process.argv[index] === '--compiler') compiler = process.argv[++index];
  else throw new Error(`Unknown argument: ${process.argv[index]}`);
}

fs.mkdirSync(out, {recursive: true});
const executable = path.join(out,
  `mhs-native-arena-owner${process.platform === 'win32' ? '.exe' : ''}`);
const compileArguments = [
  '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic'
];
if (process.platform === 'win32' && /g\+\+(?:\.exe)?$/i.test(compiler))
  compileArguments.push('-static', '-static-libgcc', '-static-libstdc++');
compileArguments.push(source, '-o', executable);
// MinGW compiler subprocesses resolve their runtime DLLs beside g++.exe.
const compilerCwd = path.isAbsolute(compiler) ? path.dirname(compiler) : root;
execFileSync(compiler, compileArguments,
  {cwd: compilerCwd, windowsHide: true, stdio: 'inherit'});

const result = JSON.parse(execFileSync(executable, [], {
  cwd: root, windowsHide: true, encoding: 'utf8'
}));
assert.equal(result.passed, true);
assert.ok(result.checks >= 40, 'transition coverage unexpectedly shrank');
assert.equal(result.capacity, 65536);
assert.equal(result.alignment, 32);
assert.equal(result.resetOnlyAbsorbing, true);
assert.equal(result.storageClears, 0);

const text = fs.readFileSync(header, 'utf8');
assert.match(text,
  /alignas\(32\)\s+static\s+DMAMEM\s+uint8_t\s+MHSNativeArenaStorage\[MHSNativeArenaCapacity\]/,
  'arena must remain an explicitly aligned DMAMEM object');
assert.match(text,
  /static\s+MHSNativeArenaControl\s+MHSNativeArenaControlState\s*;/,
  'owner, phase and generation control must remain ordinary BSS');
assert.doesNotMatch(text,
  /DMAMEM[^;\n]*MHSNativeArenaControlState/,
  'arena control must survive a reset-only RAM2 takeover');
assert.match(text, /#ifdef\s+MHS_NATIVE_ARENA_TEST[^]*MHSNativeArenaTestReset/,
  'force reset must remain test-only');
assert.doesNotMatch(text, /\b(?:memset|memcpy)\s*\(/,
  'arena ownership operations must never clear or copy storage');
for (const api of ['Claim', 'Handoff', 'Release', 'SealResetOnly',
  'Owns', 'RequiresReset', 'LeaseValid']) {
  assert.match(text, new RegExp(`MHSNativeArena${api}\\s*\\(`),
    `missing MHSNativeArena${api}`);
}

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
console.log(JSON.stringify({
  ...result,
  compiler,
  executable,
  headerSha256: sha256(fs.readFileSync(header)),
  sourceSha256: sha256(fs.readFileSync(source))
}, null, 2));
