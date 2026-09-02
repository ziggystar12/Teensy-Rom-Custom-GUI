import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const policyRoot = path.join(projectRoot, 'engine/custom-gui');
export const policy = JSON.parse(fs.readFileSync(path.join(policyRoot, 'policy.json'), 'utf8'));
export const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const normalized = bytes => bytes.toString('utf8').replaceAll('\r\n', '\n');
const read = file => fs.readFileSync(file);
const write = (file, bytes) => { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, bytes); };

function run(command, args, cwd, allowFailure = false) {
  const result = spawnSync(command, args, { cwd, encoding: 'utf8', windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
  if (!allowFailure && (result.error || result.status !== 0)) {
    throw new Error(`${command} ${args.join(' ')} failed: ${result.error?.message ?? ''}\n${result.stdout ?? ''}${result.stderr ?? ''}`);
  }
  return result;
}

export function decodeHeader(text) {
  const array = text.match(/unsigned char\s+\w+\[\]\s*=\s*\{([^}]+)\}/s);
  if (!array) throw new Error('Missing byte array in GUI asset header');
  const values = [...array[1].matchAll(/0x([0-9a-f]{2})\b/gi)];
  if (!values.length) throw new Error('Empty GUI asset header');
  return Buffer.from(values.map(value => parseInt(value[1], 16)));
}

// Parse records and preserve addresses: searching concatenated HEX text can
// otherwise claim a false match across an unmapped gap or another address bank.
export function decodeHex(text) {
  const segments = [];
  let base = 0, endSeen = false;
  for (const line of text.trim().split(/\r?\n/)) {
    if (endSeen || !/^:[0-9a-f]+$/i.test(line) || line.length % 2 !== 1) throw new Error('Invalid Intel HEX record');
    const record = Buffer.from(line.slice(1), 'hex');
    if (record.length !== record[0] + 5 || (record.reduce((sum, byte) => sum + byte, 0) & 255)) throw new Error('Intel HEX checksum/length mismatch');
    const address = record.readUInt16BE(1), type = record[3], data = record.subarray(4, -1);
    if (type === 0) {
      const absolute = base + address;
      const last = segments.at(-1);
      if (last && last.address + last.length === absolute) { last.chunks.push(data); last.length += data.length; }
      else segments.push({ address: absolute, chunks: [data], length: data.length });
    } else if (type === 1 && data.length === 0) endSeen = true;
    else if (type === 2 && data.length === 2) base = data.readUInt16BE() * 16;
    else if (type === 4 && data.length === 2) base = data.readUInt16BE() * 65536;
    else if (![3, 5].includes(type)) throw new Error(`Unsupported Intel HEX record type ${type}`);
  }
  if (!endSeen) throw new Error('Intel HEX EOF missing');
  return segments.map(segment => ({ address: segment.address, bytes: Buffer.concat(segment.chunks) }));
}

export function assertBackendScope(buffers, reviewedFiles = policy.backendFiles) {
  for (const file of reviewedFiles) {
    if (!buffers.has(file.path) || sha256(normalized(buffers.get(file.path))) !== file.sha256) {
      throw new Error(`Custom GUI backend drift requires review: ${file.path}. Only the reviewed GUI backend overlay is permitted; MPE firmware is never copied from this fork.`);
    }
  }
}

// Vendored GUI sources have their own upstream identity. The enclosing engine
// repository's HEAD is not the GUI revision and must never be reported as one.
export function verifyGuiProvenance(guiSource, files, digest, patchDigest) {
  const file = path.join(guiSource, 'provenance.json');
  const bytes = read(file);
  const provenance = JSON.parse(bytes.toString('utf8'));
  if (provenance.schemaVersion !== 1 || provenance.mode !== 'vendored-source' ||
      !/^[0-9a-f]{40}$/.test(provenance.sourceCommit ?? '') ||
      !/^https:\/\//.test(provenance.sourceRepository ?? '') ||
      provenance.snapshotDigest !== digest ||
      provenance.backendPatchSha256 !== patchDigest ||
      JSON.stringify(provenance.files) !== JSON.stringify(files)) {
    throw new Error('Vendored GUI source or provenance differs from its reviewed snapshot; update them together after review');
  }
  return { ...provenance, file, sha256: sha256(bytes) };
}

export function assertSeparatePaths(guiSource, sourcePath, snapshotRoot) {
  const contains = (parent, child) => {
    const relative = path.relative(parent, child);
    return relative === '' || (!relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative));
  };
  if (contains(guiSource, snapshotRoot) || contains(snapshotRoot, guiSource) ||
      (sourcePath && (contains(guiSource, sourcePath) || contains(sourcePath, guiSource)))) {
    throw new Error('GUI source, MHS source, and snapshot destination must be separate; the active GUI checkout is read-only');
  }
}

function findAcme(requested) {
  if (requested) return path.resolve(requested);
  const installed = path.join(os.tmpdir(), 'teensyrom-acme-0.97/unpacked/acme0.97win/acme/acme.exe');
  if (fs.existsSync(installed)) return installed;
  const found = run(process.platform === 'win32' ? 'where.exe' : 'which', ['acme'], projectRoot, true);
  if (found.status === 0) return found.stdout.trim().split(/\r?\n/)[0];
  throw new Error('ACME assembler not found; pass --acme (PowerShell builder: -CustomGuiAcmePath). No tools are downloaded by this step.');
}

function verifyAssets(snapshot, acme, buffers) {
  const cwd = path.join(snapshot, 'Source/C64/MainMenuCRT');
  fs.mkdirSync(path.join(cwd, 'build'), { recursive: true });
  for (const [unit, output, format] of [
    ['MainMenu', 'MainMenu.bin', 'plain'], ['DesktopShellCode', 'DesktopShellCode.bin', 'plain'],
    ['DesktopShell', 'DesktopShell.prg', 'cbm'], ['TeensyROMC64', 'TeensyROMC64.bin', 'plain'],
  ]) run(acme, ['--format', format, '--outfile', `build/${output}`, `source/${unit}.asm`], cwd);
  return policy.assetHeaders.map((header, index) => {
    const bytes = decodeHeader(buffers.get(header).toString('utf8'));
    const output = index === 0 ? 'TeensyROMC64.bin' : 'DesktopShell.prg';
    if (!bytes.equals(read(path.join(cwd, 'build', output)))) {
      throw new Error(`Stale generated GUI asset: ${header}. Rebuild the maintained Custom GUI menu before building MHS firmware.`);
    }
    return { header, bytes: bytes.length, sha256: sha256(bytes) };
  });
}

function checkDestination(sourcePath, buffers, overlayPaths, patch) {
  const statePath = path.join(sourcePath, '.mhs-custom-gui.json');
  const previous = fs.existsSync(statePath) ? JSON.parse(read(statePath)) : { files: [] };
  const hashes = new Map(previous.files.map(file => [file.path, file.sha256]));
  for (const relative of overlayPaths) {
    const target = path.join(sourcePath, relative);
    if (!fs.existsSync(target)) continue;
    const existing = read(target), incoming = buffers.get(relative);
    if (existing.equals(incoming) || sha256(existing) === hashes.get(relative)) continue;
    const baseline = run('git', ['show', `HEAD:${relative}`], sourcePath, true);
    if (baseline.status === 0 && normalized(existing) === baseline.stdout.replaceAll('\r\n', '\n')) continue;
    throw new Error(`Refusing to overwrite unrelated MHS source edits: ${relative}. Use a fresh pinned source clone; omit obsolete 0007-geos-desktop.patch.`);
  }
  const reverse = run('git', ['apply', '--reverse', '--check', '--ignore-space-change', patch], sourcePath, true);
  if (reverse.status === 0) return false;
  run('git', ['apply', '--check', '--ignore-space-change', patch], sourcePath);
  return true;
}

// Tests may read preview/reference sources that deliberately do not belong in
// the firmware overlay. Validate the actual applied sources in a separate tree
// while keeping those fixtures out of both the clone and immutable snapshot.
export function createAppliedSourceValidation({ sourcePath, snapshotRoot, snapshotDigest, buffers, overlayPaths }) {
  const validationPath = fs.mkdtempSync(path.join(snapshotRoot, `applied-check-${snapshotDigest.slice(0, 12)}-`));
  const appliedPaths = new Set([...overlayPaths, ...policy.backendFiles.map(file => file.path)]);
  const files = [];
  for (const [relative, reference] of buffers) {
    const target = path.join(sourcePath, relative);
    // Preserve the earlier post-apply tests' use of real MHS Teensy helpers
    // such as DriveDirLoad.ino even when they are reference-only GUI inputs.
    const actual = appliedPaths.has(relative) || (relative.startsWith('Source/Teensy/') && fs.existsSync(target));
    const bytes = actual ? read(target) : reference;
    write(path.join(validationPath, relative), bytes);
    files.push({ path: relative, sha256: sha256(bytes), source: actual ? 'applied-destination' : 'snapshot-reference' });
  }
  write(path.join(validationPath, 'package.json'), '{"private":true,"type":"commonjs"}\n');
  return { path: validationPath, files };
}

export function prepareCustomGui(options) {
  const guiSource = path.resolve(options.guiSource);
  const sourcePath = options.sourcePath ? path.resolve(options.sourcePath) : null;
  const snapshotRoot = path.resolve(options.snapshotRoot);
  assertSeparatePaths(guiSource, sourcePath, snapshotRoot);
  const menuSource = 'Source/C64/MainMenuCRT/source';
  const currentSources = fs.readdirSync(path.join(guiSource, menuSource), { withFileTypes: true })
    .filter(entry => entry.isFile() && /\.(asm|s|i)$/i.test(entry.name) && entry.name !== 'DesktopPreview.asm')
    .map(entry => `${menuSource}/${entry.name}`).sort();
  if (policy.c64SourceFiles.some(file => !currentSources.includes(file))) throw new Error('Required Custom GUI menu source is missing');
  const overlayPaths = [...currentSources, ...policy.testFiles, ...policy.assetHeaders];
  const allPaths = [...new Set([...overlayPaths, ...policy.backendFiles.map(file => file.path), ...policy.referenceOnlyFiles])].sort();
  const buffers = new Map(allPaths.map(relative => [relative, read(path.join(guiSource, relative))]));
  assertBackendScope(buffers);
  const patchPath = path.join(policyRoot, 'backend.patch');
  const patch = read(patchPath);
  const patchFiles = [...patch.toString('utf8').matchAll(/^\+\+\+ b\/(.+)$/gm)].map(match => match[1].trim());
  if (JSON.stringify(patchFiles.sort()) !== JSON.stringify(policy.backendFiles.map(file => file.path).sort())) throw new Error('GUI backend patch escapes the reviewed allowlist');
  const files = allPaths.map(relative => ({ path: relative, sha256: sha256(buffers.get(relative)), bytes: buffers.get(relative).length, role: overlayPaths.includes(relative) ? 'overlay' : 'reference-only' }));
  const digest = sha256(JSON.stringify({ files, backendPatchSha256: sha256(patch) }));
  const provenance = verifyGuiProvenance(guiSource, files, digest, sha256(patch));
  // Unique snapshots never rewrite a prior build's provenance, even if the GUI
  // checkout is being edited concurrently. No cleanup touches the source repo.
  fs.mkdirSync(snapshotRoot, { recursive: true });
  const snapshot = fs.mkdtempSync(path.join(snapshotRoot, `${digest.slice(0, 12)}-`));
  for (const [relative, bytes] of buffers) write(path.join(snapshot, relative), bytes);
  write(path.join(snapshot, 'backend.patch'), patch);
  write(path.join(snapshot, 'package.json'), '{"private":true,"type":"commonjs"}\n');
  const acme = findAcme(options.acme);
  const assets = verifyAssets(snapshot, acme, buffers);
  // The reference-only SettingsMenu source captures the fork's existing routing
  // test, but is not an additional compiled overlay: its generated PRG is stock.
  run(process.execPath, ['--test', ...policy.testFiles], snapshot);
  let referenceHex = null;
  if (options.referenceHex) {
    const hexPath = path.resolve(options.referenceHex), hex = read(hexPath), hash = sha256(hex);
    if (options.expectedReferenceSha256 && hash !== options.expectedReferenceSha256.toLowerCase()) throw new Error('Custom GUI reference HEX SHA-256 mismatch');
    const segments = decodeHex(hex.toString('utf8'));
    const embeddedAssets = assets.map(asset => {
      const bytes = decodeHeader(buffers.get(asset.header).toString('utf8'));
      const segment = segments.find(candidate => candidate.bytes.indexOf(bytes) !== -1);
      if (!segment) throw new Error(`Reference HEX does not contain current GUI asset: ${asset.header}`);
      return { header: asset.header, address: segment.address + segment.bytes.indexOf(bytes), sha256: asset.sha256 };
    });
    referenceHex = { path: hexPath, sha256: hash, bytes: hex.length, embeddedAssets, usedAsBuildInput: false };
  }
  for (const file of files) if (sha256(read(path.join(guiSource, file.path))) !== file.sha256) throw new Error(`Custom GUI changed during snapshot: ${file.path}; retry when the edit is complete`);
  if (sha256(read(provenance.file)) !== provenance.sha256) throw new Error('GUI provenance changed during snapshot; retry');
  const manifest = {
    schemaVersion: 1, mode: 'vendored-source-snapshot', sourcePath: guiSource,
    sourceHead: provenance.sourceCommit, sourceStatus: '', sourceRepository: provenance.sourceRepository,
    sourceProvenanceSha256: provenance.sha256,
    snapshotPath: snapshot, snapshotDigest: digest, backendPatchSha256: sha256(patch),
    assembler: { path: acme, sha256: sha256(read(acme)), version: run(acme, ['--version'], snapshot).stdout.trim() },
    assets, files, referenceHex, generatedAssetsMatchSource: true, focusedSourceTestsPassed: true,
    fullFirmwareBuilt: false, physicalAcceptance: false,
  };
  if (sourcePath) {
    const mustApply = checkDestination(sourcePath, buffers, overlayPaths, patchPath);
    if (mustApply) run('git', ['apply', '--ignore-space-change', '--whitespace=nowarn', patchPath], sourcePath);
    for (const relative of overlayPaths) write(path.join(sourcePath, relative), buffers.get(relative));
    // The settings routing source remains reference-only. All other tests run
    // against actual applied bytes plus the preview fixture they now require.
    const appliedValidation = createAppliedSourceValidation({ sourcePath, snapshotRoot,
      snapshotDigest: digest, buffers, overlayPaths });
    const appliedTests = policy.testFiles.filter(file => !file.includes('settings-routing'));
    run(process.execPath, ['--test', ...appliedTests], appliedValidation.path);
    manifest.appliedSourceValidation = { ...appliedValidation, tests: appliedTests, passed: true };
    write(path.join(sourcePath, '.mhs-custom-gui.json'), JSON.stringify({ snapshotDigest: digest, files: files.filter(file => file.role === 'overlay') }, null, 2) + '\n');
    manifest.appliedTo = sourcePath;
  }
  write(path.join(snapshot, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  return manifest;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2), options = {};
    const names = { '--gui-source': 'guiSource', '--source': 'sourcePath', '--snapshot-root': 'snapshotRoot', '--acme': 'acme', '--reference-hex': 'referenceHex', '--expected-reference-sha256': 'expectedReferenceSha256' };
    for (let index = 0; index < args.length; index += 2) {
      if (!names[args[index]] || !args[index + 1]) throw new Error(`Unknown or incomplete option: ${args[index]}`);
      options[names[args[index]]] = args[index + 1];
    }
    if (!options.guiSource || !options.snapshotRoot) throw new Error('Required: --gui-source PATH --snapshot-root PATH. Optional: --source MHS_CLONE --acme EXE --reference-hex HEX --expected-reference-sha256 HASH');
    console.log(JSON.stringify(prepareCustomGui(options)));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
