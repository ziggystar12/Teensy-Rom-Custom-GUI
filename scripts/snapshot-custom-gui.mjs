import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { policy, sha256, assertBackendScope } from './prepare-teensyrom-custom-gui.mjs';

const root = path.resolve(import.meta.dirname, '..');
const options = { commit: null, destination: null, acme: process.env.ACME_EXE ?? null };
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index].slice(2);
  assert.ok(key in options && process.argv[index + 1], `Unknown or incomplete ${process.argv[index]}`);
  options[key] = process.argv[index + 1];
}
assert.ok(options.commit && options.destination && options.acme,
  '--commit COMMIT --destination gui/selected-VERSION --acme PATH are required');
const git = args => execFileSync('git', args, { cwd: root, windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
const commit = git(['rev-parse', '--verify', `${options.commit}^{commit}`]).toString('utf8').trim();
const destination = path.resolve(root, options.destination);
const relative = path.relative(path.join(root, 'gui'), destination);
assert.ok(relative && !relative.startsWith('..') && !path.isAbsolute(relative), 'Snapshot must be a child of gui/');
assert.ok(!fs.existsSync(destination), 'A GUI snapshot is immutable; choose a new destination');
const menuSource = 'Source/C64/MainMenuCRT/source';
const sources = git(['ls-tree', '-r', '--name-only', commit, '--', menuSource]).toString('utf8').trim().split(/\r?\n/)
  .filter(file => /\.(asm|s|i)$/i.test(file) && !file.endsWith('/DesktopPreview.asm')).sort();
assert.ok(policy.c64SourceFiles.every(file => sources.includes(file)), 'Required C64 source is missing from the commit');
const overlays = [...sources, ...policy.helpSourceFiles, ...policy.testFiles, ...policy.assetHeaders];
const paths = [...new Set([...overlays, ...policy.backendFiles.map(file => file.path), ...policy.referenceOnlyFiles])].sort();
// Read Git blobs, never current working-tree bytes or normalized shell output.
const buffers = new Map(paths.map(file => [file, git(['show', `${commit}:${file}`])]));
assertBackendScope(buffers);
const patch = fs.readFileSync(path.join(root, 'engine/custom-gui/backend.patch'));
const patchFiles = [...patch.toString('utf8').matchAll(/^\+\+\+ b\/(.+)$/gm)].map(match => match[1].trim()).sort();
assert.deepEqual(patchFiles, policy.backendFiles.map(file => file.path).sort(), 'Backend patch scope differs from the reviewed policy');
const files = paths.map(file => ({ path: file, sha256: sha256(buffers.get(file)), bytes: buffers.get(file).length,
  role: overlays.includes(file) ? 'overlay' : 'reference-only' }));
const backendPatchSha256 = sha256(patch);
const assembler = path.resolve(options.acme);
const assemblerVersion = execFileSync(assembler, ['--version'], { windowsHide: true, encoding: 'utf8' }).trim();
assert.match(assemblerVersion, /release 0\.97\b/, 'Use the reviewed ACME 0.97 assembler');
const provenance = {
  schemaVersion: 1, mode: 'vendored-source',
  sourceRepository: 'https://github.com/ziggystar12/Teensy-Rom-Custom-GUI.git', sourceCommit: commit,
  snapshotDigest: sha256(JSON.stringify({ files, backendPatchSha256 })), backendPatchSha256,
  assembler: { sha256: sha256(fs.readFileSync(assembler)), version: assemblerVersion }, files,
};
for (const [file, bytes] of buffers) {
  const target = path.join(destination, file);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, bytes);
}
fs.writeFileSync(path.join(destination, 'provenance.json'), JSON.stringify(provenance, null, 2) + '\n');
console.log(JSON.stringify({ destination, sourceCommit: commit, snapshotDigest: provenance.snapshotDigest, files: files.length }, null, 2));
