import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';

const sourceRoot = path.resolve(import.meta.dirname, '..');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');

function makeFixture(releaseId, heapBytes) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mpe-native-release-'));
  const write = (relative, data = relative) => {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, data);
    return { file: relative.replaceAll('\\', '/'), sha256: hash(Buffer.from(data)) };
  };
  const version = releaseId === 'native18' ? '1.0.10' : '1.0.11';
  const filename = `MPE_Firmware-V${version}.hex`;
  const guiPath = `gui/selected-v${version}`;
  const guiCommit = 'a'.repeat(40), guiDigest = 'b'.repeat(64);
  const configuration = `${JSON.stringify({ schemaVersion: 1, version, releaseId,
    gui: { path: guiPath, commit: guiCommit, snapshotDigest: guiDigest } }, null, 2)}\n`;
  write('firmware-version.json', configuration);
  fs.mkdirSync(path.join(root, 'scripts'), { recursive: true });
  fs.copyFileSync(path.join(sourceRoot, 'scripts/create-native-release.mjs'),
    path.join(root, 'scripts/create-native-release.mjs'));
  write('scripts/firmware-version.mjs', `export const versionConfigurationPath='firmware-version.json';
export const firmwareVersion=${JSON.stringify({ version, releaseId, filename,
    gui: { path: guiPath, commit: guiCommit, snapshotDigest: guiDigest } })};
export function assertGuiFirmwareVersion() {}\n`);
  for (const tool of ['build-firmware.ps1', 'prepare-teensyrom-custom-gui.mjs',
    'firmware-version.mjs', 'snapshot-custom-gui.mjs']) {
    if (!fs.existsSync(path.join(root, 'scripts', tool))) write(`scripts/${tool}`);
  }

  const guiBackend = write('engine/custom-gui/backend.patch');
  write('engine/custom-gui/policy.json', '{}');
  const guiProvenance = `${JSON.stringify({ sourceRepository: 'fixture', sourceCommit: guiCommit,
    snapshotDigest: guiDigest, assembler: { sha256: 'c'.repeat(64) }, files: [] })}\n`;
  const guiProvenanceItem = write(`${guiPath}/provenance.json`, guiProvenance);
  const engineSources = [write('engine/native-game/engine.cpp')].map(item => ({
    file: path.basename(item.file), sha256: item.sha256 }));
  const nativeDosSources = Array.from({ length: 16 }, (_, index) => {
    const item = write(`engine/native-dos/dos-${index}.cpp`);
    return { file: path.basename(item.file), sha256: item.sha256 };
  });
  const patches = Array.from({ length: releaseId === 'native18' ? 45 : 46 }, (_, index) => {
    const item = write(`engine/patches/${String(index + 1).padStart(4, '0')}.patch`);
    return { path: item.file, sha256: item.sha256 };
  });
  const runtime = write('engine/native-runtime/mhs_native_arena.h');
  const cpu = write('engine/vendor/vrEmu6502/cpu.c');
  for (const file of ['.gitattributes', 'LICENSE', 'UPSTREAM.md', 'vrEmu6502.c', 'vrEmu6502.h'])
    write(`engine/vendor/vrEmu6502/${file}`);

  const artifact = write(`build/firmware/${filename}`, 'firmware');
  const restoreSource = path.join(sourceRoot, 'releases/native18/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex');
  const restoreDestination = path.join(root, 'build/firmware/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex');
  fs.copyFileSync(restoreSource, restoreDestination);
  const restoreBytes = fs.readFileSync(restoreDestination);
  write('docs/FIRMWARE-GUIDE.md', 'guide');
  write('build/firmware/MHS-POWER-ENGINE.md', 'guide');

  const proof = {
    buildProfile: releaseId, mpeFirmwareVersion: version, artifact: filename,
    firmwareFilename: filename, versionConfiguration: { sha256: hash(Buffer.from(configuration)) },
    minimalBootStackReserveBytes: 16384, minimalBootRam2HeapReserveBytes: heapBytes,
    customGui: { sourceHead: guiCommit, snapshotDigest: guiDigest,
      sourceProvenanceSha256: guiProvenanceItem.sha256, backendPatchSha256: guiBackend.sha256 },
    nativeGameSources: engineSources, nativeDosSources, patches,
    ...(releaseId === 'native19' ? { nativeRuntimeSources: [{ file: path.basename(runtime.file), sha256: runtime.sha256 }] } : {}),
    compiledVendorSources: [{ file: path.basename(cpu.file), sha256: cpu.sha256 }],
    sha256: artifact.sha256, bytes: Buffer.byteLength('firmware'),
    officialRestoreSha256: hash(restoreBytes), upstream: 'fixture', upstreamCommit: 'd'.repeat(40),
    arduinoCliVersion: 'fixture', teensyCoreVersion: 'fixture', crc32LibraryVersion: 'fixture'
  };
  write('build/manifests/firmware-build.json', `${JSON.stringify(proof)}\n`);
  return root;
}

function runFixture(releaseId, heapBytes) {
  const root = makeFixture(releaseId, heapBytes);
  try {
    const result = spawnSync(process.execPath,
      ['scripts/create-native-release.mjs', '--build', 'build', '--release', releaseId],
      { cwd: root, encoding: 'utf8', windowsHide: true });
    return { ...result, manifest: result.status === 0
      ? JSON.parse(fs.readFileSync(path.join(root, `releases/${releaseId}/manifest.json`), 'utf8')) : null };
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

test('native19 preserves its recovered RAM2 while native18 retains its historical floor', () => {
  const historical = runFixture('native18', 256 * 1024);
  assert.equal(historical.status, 0, historical.stderr);
  const below = runFixture('native19', 320 * 1024 - 1);
  assert.notEqual(below.status, 0);
  assert.match(below.stderr, /native19 must retain at least 320 KiB/);
  const exact = runFixture('native19', 320 * 1024);
  assert.equal(exact.status, 0, exact.stderr);
  assert.equal(exact.manifest.memory.minimalBootRam2HeapReserveBytes, 320 * 1024);
  assert.equal(exact.manifest.scope,
    'MHS Power Engine firmware with the selected GUI; AGI-compatible game cartridges are built separately.');
});
