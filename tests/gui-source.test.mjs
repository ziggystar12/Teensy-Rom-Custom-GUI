import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { policy, sha256, assertBackendScope, verifyGuiProvenance } from '../scripts/prepare-teensyrom-custom-gui.mjs';

const root = path.resolve(import.meta.dirname, '..');
const gui = path.join(root, 'gui/selected-e305');
const provenance = JSON.parse(fs.readFileSync(path.join(gui, 'provenance.json'), 'utf8'));
const release = JSON.parse(fs.readFileSync(path.join(root, 'releases/native05/manifest.json'), 'utf8'));
const bytes = file => fs.readFileSync(file);

test('release and all native engine, patch and vendor inputs retain their exact native05 hashes', () => {
  assert.equal(release.engineSources.length, 9);
  assert.equal(release.patches.length, 36);
  for (const file of release.files) {
    const data = bytes(path.join(root, 'releases/native05', file.file));
    assert.equal(data.length, file.bytes, file.file);
    assert.equal(sha256(data), file.sha256, file.file);
  }
  for (const file of [...release.engineSources, ...release.patches, ...release.vendorSources]) {
    const data = bytes(path.join(root, file.file));
    assert.equal(data.length, file.bytes, file.file);
    assert.equal(sha256(data), file.sha256, file.file);
  }
});

test('the 49 selected GUI files and backend identify e305 independently of the engine Git HEAD', () => {
  assert.equal(provenance.files.length, 49);
  const files = provenance.files.map(file => {
    const data = bytes(path.join(gui, file.path));
    return { path: file.path, sha256: sha256(data), bytes: data.length, role: file.role };
  });
  const patchDigest = sha256(bytes(path.join(root, 'engine/custom-gui/backend.patch')));
  const digest = sha256(JSON.stringify({ files, backendPatchSha256: patchDigest }));
  const result = verifyGuiProvenance(gui, files, digest, patchDigest);
  assert.equal(result.sourceCommit, 'e305f6dc24c526b1e337e9718fbb71d599ed70d8');
  assert.equal(result.sourceCommit, release.customGuiCommit);
  assert.equal(digest, 'c574929263728ebae17064bbe5a7d48941b33db931f62121476734cb25eda7a3');
  assertBackendScope(new Map(policy.backendFiles.map(file => [file.path, bytes(path.join(gui, file.path))])));
});

test('GUI source changes, omitted inputs and backend changes fail the provenance gate', () => {
  const files = structuredClone(provenance.files);
  files[0].sha256 = '0'.repeat(64);
  assert.throws(() => verifyGuiProvenance(gui, files, provenance.snapshotDigest, provenance.backendPatchSha256), /differs/);
  assert.throws(() => verifyGuiProvenance(gui, provenance.files.slice(1), provenance.snapshotDigest, provenance.backendPatchSha256), /differs/);
  assert.throws(() => verifyGuiProvenance(gui, provenance.files, provenance.snapshotDigest, '0'.repeat(64)), /differs/);
});
