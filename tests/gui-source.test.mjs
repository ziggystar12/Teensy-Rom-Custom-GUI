import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { policy, sha256, assertBackendScope, verifyGuiProvenance, assertGuiBuildSizes } from '../scripts/prepare-teensyrom-custom-gui.mjs';

const root = path.resolve(import.meta.dirname, '..');
const gui = path.join(root, 'gui/selected-ac4a5d6');
const bytes = file => fs.readFileSync(file);
const json = file => JSON.parse(bytes(file).toString('utf8'));
const provenance = json(path.join(gui, 'provenance.json'));
const selectedCommit = 'ac4a5d6ce3d8037d4fdd7eee58899b9bc7463b3e';
const selectedDigest = '3cba53dc478e6e69d6bc17a4cd243d2e8b3fa7a9f1778184fda78a0d552f10dd';
const checkFile = (base, file) => {
  const data = bytes(path.join(base, file.file));
  assert.equal(data.length, file.bytes, file.file);
  assert.equal(sha256(data), file.sha256, file.file);
};

// Historical release payloads remain immutable. Their old engine hashes name
// their source revision, not the current engine files after later fixes.
for (const name of ['native05', 'native06', 'native07']) {
  test(`${name} firmware, restore image and guide retain their recorded bytes`, () => {
    const directory = path.join(root, 'releases', name), release = json(path.join(directory, 'manifest.json'));
    assert.equal(release.releaseId, name);
    assert.equal(release.engineSources.length, 9);
    assert.equal(release.patches.length, name === 'native05' ? 36 : 37);
    assert.equal(release.files.length, 3);
    for (const file of release.files) checkFile(directory, file);
  });
}

test('the complete selected GUI source and reviewed backend identify ac4a5d6', () => {
  const menuSource = 'Source/C64/MainMenuCRT/source';
  const sources = fs.readdirSync(path.join(gui, menuSource), { withFileTypes: true })
    .filter(entry => entry.isFile() && /\.(asm|s|i)$/i.test(entry.name) && entry.name !== 'DesktopPreview.asm')
    .map(entry => `${menuSource}/${entry.name}`).sort();
  const overlay = [...sources, ...policy.testFiles, ...policy.assetHeaders];
  const required = [...new Set([...overlay, ...policy.backendFiles.map(file => file.path), ...policy.referenceOnlyFiles])].sort();
  assert.equal(sources.length, 25);
  assert.equal(policy.testFiles.length, 16);
  assert.equal(required.length, 68);
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

const currentRelease = path.join(root, 'releases/native08/manifest.json');
test('native08 release records the current engine, tools, backend and selected GUI',
  { skip: !fs.existsSync(currentRelease) && 'native08 has not been created yet' }, () => {
    const release = json(currentRelease);
    assert.equal(release.releaseId, 'native08');
    assert.equal(release.customGuiCommit, selectedCommit);
    assert.equal(release.gui.snapshotDigest, selectedDigest);
    assert.equal(release.engineSources.length, 9);
    assert.equal(release.patches.length, 37);
    for (const file of release.files) checkFile(path.dirname(currentRelease), file);
    for (const file of [...release.engineSources, ...release.patches, ...release.vendor, ...release.buildTools,
      release.gui.provenance, release.gui.backend, release.gui.policy]) checkFile(root, file);
  });
