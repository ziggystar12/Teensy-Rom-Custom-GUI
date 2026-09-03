import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { policy, sha256, assertBackendScope, verifyGuiProvenance, assertGuiBuildSizes } from '../scripts/prepare-teensyrom-custom-gui.mjs';
import { firmwareVersion, validateFirmwareVersion, assertGuiFirmwareVersion } from '../scripts/firmware-version.mjs';

const root = path.resolve(import.meta.dirname, '..');
const gui = path.join(root, firmwareVersion.gui.path);
const bytes = file => fs.readFileSync(file);
const json = file => JSON.parse(bytes(file).toString('utf8'));
const provenance = json(path.join(gui, 'provenance.json'));
const selectedCommit = firmwareVersion.gui.commit;
const selectedDigest = firmwareVersion.gui.snapshotDigest;
const checkFile = (base, file) => {
  const data = bytes(path.join(base, file.file));
  assert.equal(data.length, file.bytes, file.file);
  assert.equal(sha256(data), file.sha256, file.file);
};

// Historical release payloads remain immutable. Their old engine hashes name
// their source revision, not the current engine files after later fixes.
for (const name of ['native05', 'native06', 'native07', 'native08', 'native09', 'native10', 'native11', 'native12', 'native13', 'native14', 'native15', 'native16']) {
  test(`${name} firmware, restore image and guide retain their recorded bytes`, () => {
    const directory = path.join(root, 'releases', name), release = json(path.join(directory, 'manifest.json'));
    assert.equal(release.releaseId, name);
    assert.equal(release.engineSources.length, 9);
    assert.equal(release.patches.length, name === 'native05' ? 36 : 37);
    assert.equal(release.files.length, 3);
    for (const file of release.files) checkFile(directory, file);
  });
}

test('native09 retains its exact published 75-file V1.0.1 GUI snapshot', () => {
  const release = json(path.join(root, 'releases/native09/manifest.json'));
  checkFile(root, release.gui.provenance);
  const directory = path.join(root, 'gui/selected-v1.0.1');
  const old = json(path.join(directory, 'provenance.json'));
  assert.equal(old.sourceCommit, '14ef9df71b17c058bdeba103cbe5f452d064345a');
  assert.equal(old.snapshotDigest, 'db2c1e6cc1579f6476067abf0500524202d8214f46a955c7676d6cdd50a84120');
  assert.equal(old.files.length, 75);
  assert.equal(old.backendPatchSha256, release.gui.backend.sha256);
  const files = old.files.map(file => {
    const data = bytes(path.join(directory, file.path));
    assert.equal(data.length, file.bytes, file.path);
    assert.equal(sha256(data), file.sha256, file.path);
    return { path: file.path, sha256: sha256(data), bytes: data.length, role: file.role };
  });
  assert.equal(sha256(JSON.stringify({ files, backendPatchSha256: old.backendPatchSha256 })), old.snapshotDigest);
  assert.equal(release.customGuiCommit, old.sourceCommit);
  assert.equal(release.gui.snapshotDigest, old.snapshotDigest);
  assert.equal(release.files[0].file, 'MPE_Firmware-V1.0.1.hex');
});

for (const name of ['native10', 'native11', 'native12', 'native13', 'native14', 'native15', 'native16']) {
  test(`${name} retains every published GUI snapshot input`, () => {
    const release = json(path.join(root, 'releases', name, 'manifest.json'));
    checkFile(root, release.gui.provenance);
    const directory = path.dirname(path.join(root, release.gui.provenance.file));
    const old = json(path.join(directory, 'provenance.json'));
    assert.equal(old.sourceCommit, release.customGuiCommit);
    assert.equal(old.snapshotDigest, release.gui.snapshotDigest);
    assert.equal(old.backendPatchSha256, release.gui.backend.sha256);
    const files = old.files.map(file => {
      const data = bytes(path.join(directory, file.path));
      assert.equal(data.length, file.bytes, file.path);
      assert.equal(sha256(data), file.sha256, file.path);
      return { path: file.path, sha256: sha256(data), bytes: data.length, role: file.role };
    });
    assert.equal(sha256(JSON.stringify({ files, backendPatchSha256: old.backendPatchSha256 })), old.snapshotDigest);
  });
}

test('the complete selected GUI source and reviewed backend identify the current firmware version', () => {
  const menuSource = 'Source/C64/MainMenuCRT/source';
  const sources = fs.readdirSync(path.join(gui, menuSource), { withFileTypes: true })
    .filter(entry => entry.isFile() && /\.(asm|s|i)$/i.test(entry.name) && entry.name !== 'DesktopPreview.asm')
    .map(entry => `${menuSource}/${entry.name}`).sort();
  const overlay = [...sources, ...policy.helpSourceFiles, ...policy.settingsSourceFiles, ...policy.testFiles, ...policy.assetHeaders];
  const required = [...new Set([...overlay, ...policy.backendFiles.map(file => file.path), ...policy.referenceOnlyFiles])].sort();
  assert.equal(sources.length, 31);
  assert.equal(policy.testFiles.length, 33);
  assert.equal(policy.settingsSourceFiles.length, 12);
  assert.equal(policy.assetHeaders.length, 4);
  assert.equal(required.length, 129);
  const files = required.map(relative => {
    const data = bytes(path.join(gui, relative));
    return { path: relative, sha256: sha256(data), bytes: data.length,
      role: overlay.includes(relative) ? 'overlay' : 'reference-only' };
  });
  const patchDigest = sha256(bytes(path.join(root, 'engine/custom-gui/backend.patch')));
  const digest = sha256(JSON.stringify({ files, backendPatchSha256: patchDigest }));
  const result = verifyGuiProvenance(gui, files, digest, patchDigest);
  assert.equal(result.sourceCommit, selectedCommit);
  assert.equal(digest, selectedDigest);
  assertBackendScope(new Map(policy.backendFiles.map(file => [file.path, bytes(path.join(gui, file.path))])));
});

test('the version configuration derives the filename and matches the actual GUI About version', () => {
  assert.equal(firmwareVersion.filename, `MPE_Firmware-V${firmwareVersion.version}.hex`);
  assertGuiFirmwareVersion();
  const next = structuredClone(firmwareVersion);
  const parts = next.version.split('.').map(Number);
  parts[2]++;
  next.version = parts.join('.');
  next.gui.path = `gui/selected-v${next.version}`;
  assert.equal(validateFirmwareVersion(next).filename, `MPE_Firmware-V${next.version}.hex`);
  assert.throws(() => validateFirmwareVersion({ ...next, version: '1.0.01' }), /Invalid firmware version/);
  assert.throws(() => validateFirmwareVersion({ ...next, releaseId: '../native09' }), /Invalid release id/);
  assert.throws(() => validateFirmwareVersion({ ...next, gui: { ...next.gui, path: '../outside' } }), /snapshot/);
});

test('GUI source changes, omitted inputs and backend changes fail the provenance gate', () => {
  const files = structuredClone(provenance.files);
  files[0].sha256 = '0'.repeat(64);
  assert.throws(() => verifyGuiProvenance(gui, files, provenance.snapshotDigest, provenance.backendPatchSha256), /differs/);
  assert.throws(() => verifyGuiProvenance(gui, provenance.files.slice(1), provenance.snapshotDigest, provenance.backendPatchSha256), /differs/);
  assert.throws(() => verifyGuiProvenance(gui, provenance.files, provenance.snapshotDigest, '0'.repeat(64)), /differs/);
});

test('resident desktop and app payload sizes cannot cross their C64 RAM regions', () => {
  assert.doesNotThrow(() => assertGuiBuildSizes(0x5800, 0x1000));
  assert.throws(() => assertGuiBuildSizes(0x5801, 0x1000), /Desktop shell/);
  assert.throws(() => assertGuiBuildSizes(0x5800, 0x1001), /Resident desktop apps/);
  assert.throws(() => assertGuiBuildSizes(0, 1), /Desktop shell/);
  assert.throws(() => assertGuiBuildSizes(1, 0), /Resident desktop apps/);
});

// Development tools on main can move beyond the released firmware. Bind the
// recorded tools to the source-lock commit containing this exact immutable
// manifest; never fall back to HEAD or the working tree if the lock is stale.
function checkLockedBuildTools(release, relativeManifest, sourceLock) {
  assert.match(sourceLock.engineCommit ?? '', /^[0-9a-f]{40}$/, 'source lock requires a full commit hash');
  for (const field of ['releaseId', 'mpeFirmwareVersion', 'firmwareFilename', 'customGuiCommit'])
    assert.equal(sourceLock[field], release[field], `source lock ${field}`);
  const commit = spawnSync('git', ['cat-file', '-t', sourceLock.engineCommit],
    { cwd: root, windowsHide: true, encoding: 'utf8' });
  assert.ifError(commit.error);
  assert.equal(commit.status, 0, `Missing locked release source commit: ${commit.stderr}`);
  assert.equal(commit.stdout.trim(), 'commit', 'source lock must identify a commit object');
  const readBlob = relative => {
    const result = spawnSync('git', ['cat-file', 'blob', `${sourceLock.engineCommit}:${relative}`],
      { cwd: root, windowsHide: true, maxBuffer: 4 * 1024 * 1024 });
    assert.ifError(result.error);
    assert.equal(result.status, 0, `Missing locked release source ${relative}: ${result.stderr.toString()}`);
    return result.stdout;
  };
  const pinnedManifest = readBlob(relativeManifest);
  assert.ok(pinnedManifest.equals(Buffer.from(JSON.stringify(release, null, 2) + '\n')),
    'source-lock commit must contain the exact release manifest');
  assert.ok(pinnedManifest.equals(bytes(path.join(root, relativeManifest))),
    'local immutable manifest must match its source-lock commit byte for byte');
  for (const file of release.buildTools) {
    const data = readBlob(file.file);
    assert.equal(data.length, file.bytes, `locked ${file.file}`);
    assert.equal(sha256(data), file.sha256, `locked ${file.file}`);
  }
}

test('locked release build tools require the exact committed manifest and matching source identity', () => {
  const relative = 'releases/native15/manifest.json', release = json(path.join(root, relative));
  const sourceLock = { engineCommit: 'ac2c8ef11c1365570a46f645daf8c98c5951aece',
    releaseId: release.releaseId, mpeFirmwareVersion: release.mpeFirmwareVersion,
    firmwareFilename: release.firmwareFilename, customGuiCommit: release.customGuiCommit };
  assert.doesNotThrow(() => checkLockedBuildTools(release, relative, sourceLock));
  assert.throws(() => checkLockedBuildTools(release, relative, { ...sourceLock, engineCommit: 'HEAD' }), /full commit hash/);
  assert.throws(() => checkLockedBuildTools(release, relative, { ...sourceLock, releaseId: 'native00' }), /source lock releaseId/);
  assert.throws(() => checkLockedBuildTools(release, relative, { ...sourceLock, engineCommit: '0'.repeat(40) }), /Missing locked release source/);
  const changed = structuredClone(release);
  changed.buildTools[0].sha256 = '0'.repeat(64);
  assert.throws(() => checkLockedBuildTools(changed, relative, sourceLock), /exact release manifest/);
});

const currentRelease = path.join(root, 'releases', firmwareVersion.releaseId, 'manifest.json');
test(`${firmwareVersion.releaseId} release records the current engine, locked build tools, backend and selected GUI`,
  { skip: !fs.existsSync(currentRelease) && `${firmwareVersion.releaseId} has not been created yet` }, () => {
    const release = json(currentRelease);
    assert.equal(release.releaseId, firmwareVersion.releaseId);
    assert.equal(release.mpeFirmwareVersion, firmwareVersion.version);
    assert.equal(release.firmwareFilename, firmwareVersion.filename);
    assert.equal(release.files[0].file, firmwareVersion.filename);
    assert.equal(release.customGuiCommit, selectedCommit);
    assert.equal(release.gui.snapshotDigest, selectedDigest);
    assert.equal(release.engineSources.length, 9);
    assert.equal(release.patches.length, 37);
    for (const file of release.files) checkFile(path.dirname(currentRelease), file);
    for (const file of [...release.engineSources, ...release.patches, ...release.vendor,
      release.gui.provenance, release.gui.backend, release.gui.policy, release.versionConfiguration]) checkFile(root, file);
    checkLockedBuildTools(release, `releases/${firmwareVersion.releaseId}/manifest.json`,
      json(path.join(root, 'docs/firmware/source.lock.json')));
  });
