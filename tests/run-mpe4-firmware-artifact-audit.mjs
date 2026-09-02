import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { firmwareVersion, versionConfigurationPath, assertGuiFirmwareVersion } from '../scripts/firmware-version.mjs';

const root = path.resolve(import.meta.dirname, '..');
const options = {
  source: null, build: null, out: null, 'native-result': null,
  raw: null, intro: null, 'arm-tools': null
};
if (process.argv.includes('--help')) {
  console.log('Read-only MPE4 final firmware audit: --source DIR --build DIR --out JSON --native-result JSON --raw BIN --intro BIN --arm-tools DIR');
  process.exit(0);
}
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index].slice(2);
  assert.ok(process.argv[index].startsWith('--') && key in options && process.argv[index + 1], 'Invalid audit argument');
  options[key] = path.resolve(process.argv[index + 1]);
}
for (const [key, value] of Object.entries(options)) assert.ok(value, `--${key} is required`);
const { source, build, out: output, 'native-result': nativeResultPath } = options;
// Historical releases keep their original pins. The current release uses the
// central version configuration, never the enclosing checkout's current HEAD.
const previousGui = {
  commit: 'e305f6dc24c526b1e337e9718fbb71d599ed70d8',
  snapshotDigest: 'c574929263728ebae17064bbe5a7d48941b33db931f62121476734cb25eda7a3'
};
const tools = options['arm-tools'];
const { decodeHex, decodeHeader, sha256 } = await import(pathToFileURL(path.join(root, 'scripts/prepare-teensyrom-custom-gui.mjs')));
const auditedInputs = new Map();
const read = file => {
  const absolute = path.resolve(file), bytes = fs.readFileSync(absolute), digest = sha256(bytes);
  if (auditedInputs.has(absolute)) assert.equal(digest, auditedInputs.get(absolute), `Audit input changed while being read: ${absolute}`);
  else auditedInputs.set(absolute, digest);
  return bytes;
};
const json = file => JSON.parse(read(file).toString('utf8'));
const hexAddress = value => `0x${value.toString(16)}`;

function safeChild(parent, relative) {
  const target = path.resolve(parent, relative);
  const relation = path.relative(path.resolve(parent), target);
  assert.ok(relation && !relation.startsWith(`..${path.sep}`) && relation !== '..' && !path.isAbsolute(relation),
    `Manifest path escapes its directory: ${relative}`);
  return target;
}

function hexSegments(file) {
  const segments = decodeHex(read(file).toString('utf8')).sort((a, b) => a.address - b.address);
  for (let index = 1; index < segments.length; index++) {
    assert.ok(segments[index - 1].address + segments[index - 1].bytes.length <= segments[index].address,
      `${file} contains overlapping mapped segments`);
  }
  return segments;
}

function bytesAt(segments, address, length) {
  const pieces = [];
  let cursor = address;
  let remaining = length;
  while (remaining) {
    const segment = segments.find(candidate => cursor >= candidate.address && cursor < candidate.address + candidate.bytes.length);
    assert.ok(segment, `Missing combined HEX data at ${hexAddress(cursor)}`);
    const offset = cursor - segment.address;
    const size = Math.min(remaining, segment.bytes.length - offset);
    pieces.push(segment.bytes.subarray(offset, offset + size));
    remaining -= size; cursor += size;
  }
  return Buffer.concat(pieces);
}

function embeddedLocations(segments, bytes) {
  const result = [];
  for (const segment of segments) {
    for (let cursor = segment.bytes.indexOf(bytes); cursor >= 0; cursor = segment.bytes.indexOf(bytes, cursor + 1)) {
      result.push(segment.address + cursor);
    }
  }
  return result;
}

function symbolsFor(elf) {
  const stdout = execFileSync(path.join(tools, process.platform==='win32'?'arm-none-eabi-nm.exe':'arm-none-eabi-nm'), ['-C', '-S', '-n', elf],
    { encoding: 'utf8', windowsHide: true, maxBuffer: 16 * 1024 * 1024 });
  const symbols = new Map();
  for (const line of stdout.split(/\r?\n/)) {
    const sized = line.match(/^([0-9a-f]+)\s+([0-9a-f]+)\s+(\S)\s+(.+)$/i);
    const plain = line.match(/^([0-9a-f]+)\s+\S\s+(.+)$/i);
    if (sized) symbols.set(sized[4], { symbol: sized[4], kind: sized[3], address: parseInt(sized[1], 16), bytes: parseInt(sized[2], 16) });
    else if (plain) symbols.set(plain[2], { symbol: plain[2], address: parseInt(plain[1], 16), bytes: null });
  }
  return symbols;
}

function requiredSymbol(symbols, name) {
  const value = symbols.get(name);
  assert.ok(value, `Missing linked symbol ${name}`);
  return value;
}

function verifyNativeInputInterrupts(elf, symbols) {
  requiredSymbol(symbols, 'MPE4NextPacket()');
  // Cover the poller and any factored MPE4 input helper in its native glue.
  // Inspect the linked instructions: host stubs and source text cannot prove
  // that FLASHMEM input handling leaves the time-critical PHI2 IRQ enabled.
  const entries = [...symbols.values()].filter(entry => /[tT]/.test(entry.kind ?? '') && /^MPE4\w+\(/.test(entry.symbol));
  const checked = entries.map(entry => {
    assert.ok(entry.bytes > 0, `Missing linked code extent for ${entry.symbol}`);
    const assembly = execFileSync(path.join(tools, process.platform==='win32'?'arm-none-eabi-objdump.exe':'arm-none-eabi-objdump'),
      ['-d', '-C', `--start-address=${hexAddress(entry.address)}`, `--stop-address=${hexAddress(entry.address + entry.bytes)}`, elf],
      { encoding: 'utf8', windowsHide: true, maxBuffer: 4 * 1024 * 1024 });
    assert.doesNotMatch(assembly, /\bcpsid(?:\.\w+)?\s+[if]\b|\bmsr(?:\.\w+)?\s+(?:primask|faultmask|basepri(?:_max)?)\b|\b(?:blx?|b(?:\.w)?)\s+[^\n]*<(?:__disable_irq|noInterrupts)\b/i,
      `${entry.symbol} masks interrupts in linked firmware and can miss PHI2 bus cycles`);
    return { symbol: entry.symbol, address: hexAddress(entry.address), bytes: entry.bytes, noGlobalInterruptMasks: true };
  });
  return { root: 'MPE4NextPacket()', noGlobalInterruptMasks: true, checked };
}

function sectionsFor(elf) {
  const stdout = execFileSync(path.join(tools, process.platform==='win32'?'arm-none-eabi-objdump.exe':'arm-none-eabi-objdump'), ['-h', elf],
    { encoding: 'utf8', windowsHide: true, maxBuffer: 4 * 1024 * 1024 });
  const sections = new Map();
  for (const line of stdout.split(/\r?\n/)) {
    const found = line.match(/^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+/i);
    if (found) sections.set(found[1], { bytes: parseInt(found[2], 16), address: parseInt(found[3], 16), loadAddress: parseInt(found[4], 16) });
  }
  return sections;
}

function verifyImage(name, relative, combined) {
  const stem = name === 'full' ? 'Teensy.ino' : 'MinimalBoot.ino';
  const elf = path.join(source, relative, `${stem}.elf`);
  const linkedHex = path.join(source, relative, `${stem}.hex`);
  const elfSha256 = sha256(read(elf));
  const linked = hexSegments(linkedHex);
  for (const segment of linked) {
    assert.deepEqual(bytesAt(combined, segment.address, segment.bytes.length), segment.bytes,
      `${name} linked image differs from combined HEX at ${hexAddress(segment.address)}`);
  }
  const symbols = symbolsFor(elf);
  const sections = sectionsFor(elf);
  const itcm = sections.get('.text.itcm');
  assert.ok(itcm && itcm.address === 0 && itcm.bytes > 0 && itcm.bytes <= 512 * 1024, `${name} ITCM extent is invalid`);
  const handlers = ['isrPHI2()', 'IO1Hndlr_EasyFlash(unsigned char, bool)', 'IO2Hndlr_EasyFlash(unsigned char, bool)',
    ...(name === 'minimalBoot' ? ['MPE3TitleIO2Hndlr(unsigned char, bool)'] : [])];
  const busHandlers = handlers.map(symbol => {
    const entry = requiredSymbol(symbols, symbol);
    assert.ok(entry.bytes > 0 && entry.address >= itcm.address && entry.address + entry.bytes <= itcm.address + itcm.bytes,
      `${symbol} is not entirely inside linked fast instruction RAM`);
    return { ...entry, address: hexAddress(entry.address), entirelyInITCM: true };
  });
  const stackReserveBytes = requiredSymbol(symbols, '_estack').address - requiredSymbol(symbols, '_ebss').address;
  const heapStart = requiredSymbol(symbols, '_heap_start').address;
  const heapEnd = requiredSymbol(symbols, '_heap_end').address;
  const ram2HeapReserveBytes = heapEnd - heapStart;
  assert.ok(stackReserveBytes >= 16 * 1024, `${name} stack reserve is below 16 KiB`);
  assert.ok(heapStart >= 0x20200000 && heapEnd <= 0x20280000 && ram2HeapReserveBytes >= 256 * 1024,
    `${name} RAM2 heap range or reserve fails its guard`);
  let nativeArena = null;
  let nativeCartridgeIndex = null;
  let nativeFlash = null;
  let nativeInputInterrupts = null;
  if (name === 'minimalBoot') {
    const entry = requiredSymbol(symbols, 'MPE3TitleInternalAssets');
    assert.equal(entry.bytes, 65536, 'native title arena must remain 64 KiB');
    assert.ok(entry.address >= 0x20200000 && entry.address + entry.bytes <= heapStart,
      'native title arena must remain entirely within internal RAM2 below the heap');
    nativeArena = { ...entry, address: hexAddress(entry.address), internallyResident: true };
    if (extendedCartridge) {
      const index = requiredSymbol(symbols, 'MPE4CrtDirectory');
      assert.equal(index.bytes, 2052, 'Native cartridge index must remain bounded');
      assert.ok(index.address >= 0x20200000 && index.address + index.bytes <= heapStart,
        'Native cartridge index must remain entirely within RAM2 below the heap');
      nativeCartridgeIndex = { ...index, address: hexAddress(index.address), internallyResident: true };
    }
    const flash = sections.get('.text.code');
    assert.ok(flash && flash.address >= 0x60000000 && flash.address + flash.bytes <= 0x60800000,
      'Native gameplay FLASH code section is missing or outside internal flash');
    const methods = [...symbols.values()].filter(entry => /[tT]/.test(entry.kind ?? '') &&
      (entry.symbol.startsWith('mpe4::') || /^MPE4\w+\(/.test(entry.symbol)));
    for (const prefix of ['mpe4::Game::tick(', 'mpe4::Game::parse(', 'mpe4::Renderer::render(',
      'mpe4::Renderer::drawPicture(', 'mpe4::Package::open(', 'mpe4::Session::start(',
      'mpe4::Session::prepareFrame(', 'MPE4NextPacket(', 'MPE4Save(', 'MPE4Restore(']) {
      assert.ok(methods.some(entry => entry.symbol.startsWith(prefix)), `Missing linked native method ${prefix}`);
    }
    methods.push(requiredSymbol(symbols, 'AGIPictureInit()'));
    if (extendedCartridge) methods.push(requiredSymbol(symbols, 'LoadFile(StructMenuItem*, FS*)'));
    nativeFlash = methods.map(entry => {
      assert.ok(entry.bytes > 0 && entry.address >= flash.address && entry.address + entry.bytes <= flash.address + flash.bytes,
        `${entry.symbol} must remain entirely in FLASH code, preserving instruction RAM and stack`);
      return { ...entry, address: hexAddress(entry.address), entirelyInFLASH: true };
    });
    if (currentProfile) nativeInputInterrupts = verifyNativeInputInterrupts(elf, symbols);
  }
  const launch = name === 'full' ? ['CRTRequiresMPE3MinimalBoot(unsigned char const*)', 'LaunchCRTInMinimal(char const*)']
    .map(symbol => { const entry = requiredSymbol(symbols, symbol); return { ...entry, address: hexAddress(entry.address) }; }) : [];
  return {
    elf, elfSha256, linkedHex, linkedHexSha256: sha256(read(linkedHex)),
    combinedImageByteExact: true, embeddedBytes: linked.reduce((sum, segment) => sum + segment.bytes.length, 0),
    itcm: { address: hexAddress(itcm.address), bytes: itcm.bytes }, busHandlers, launch,
    stackReserveBytes, ram2HeapReserveBytes, memoryThresholdsPassed: true, nativeArena, nativeCartridgeIndex, nativeFlash, nativeInputInterrupts,
    _segments: linked
  };
}

function verifyGui(gui, combined, fullSegments) {
  assert.equal(gui.sourceHead, selectedGui.commit, 'GUI must use the exact commit selected by the user');
  assert.equal(gui.snapshotDigest, selectedGui.snapshotDigest, 'GUI content differs from the selected clean commit snapshot');
  if (gui.mode === 'vendored-source-snapshot') {
    const provenanceBytes=read(safeChild(gui.sourcePath,'provenance.json'));
    assert.equal(sha256(provenanceBytes),gui.sourceProvenanceSha256,'Vendored GUI provenance changed after the build');
    const provenance=JSON.parse(provenanceBytes.toString('utf8'));
    assert.equal(provenance.sourceCommit,selectedGui.commit,'Vendored GUI source pin differs from the selected commit');
    assert.equal(provenance.snapshotDigest,selectedGui.snapshotDigest,'Vendored GUI content pin differs from the selected snapshot');
    assert.deepEqual(provenance.files,gui.files,'Vendored GUI source inventory differs from the applied build inventory');
    for(const file of provenance.files){
      const actual=read(safeChild(gui.sourcePath,file.path));
      assert.equal(actual.length,file.bytes,`Vendored GUI source size differs: ${file.path}`);
      assert.equal(sha256(actual),file.sha256,`Vendored GUI source hash differs: ${file.path}`);
    }
  } else {
    const actualHead = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: gui.sourcePath,
      encoding: 'utf8', windowsHide: true }).trim();
    assert.equal(actualHead, selectedGui.commit, 'GUI source checkout moved after the selected snapshot');
    const maintainedStatus = execFileSync('git', ['status', '--porcelain', '--', ...gui.files.map(file => file.path)], {
      cwd: gui.sourcePath, encoding: 'utf8', windowsHide: true }).trim();
    assert.equal(maintainedStatus, '', 'Selected GUI source checkout contains modified maintained inputs');
  }
  assert.ok(gui.generatedAssetsMatchSource && gui.focusedSourceTestsPassed, 'GUI source and generated assets did not pass builder gates');
  assert.ok(gui.appliedSourceValidation?.passed, 'Applied GUI source tests were not recorded as passing');
  assert.equal(path.resolve(gui.appliedTo), path.resolve(source), 'GUI manifest refers to a different source clone');
  const files = gui.files.map(file => {
    const actual = read(safeChild(gui.snapshotPath, file.path));
    assert.equal(actual.length, file.bytes, `GUI snapshot size differs: ${file.path}`);
    assert.equal(sha256(actual), file.sha256, `GUI snapshot hash differs: ${file.path}`);
    if (file.role === 'overlay') {
      assert.equal(sha256(read(safeChild(source, file.path))), file.sha256, `Applied GUI overlay differs from snapshot: ${file.path}`);
    }
    return { path: file.path, bytes: file.bytes, sha256: file.sha256, snapshotMatches: true,
      appliedOverlayMatches: file.role === 'overlay' };
  });
  const backend = read(path.join(gui.snapshotPath, 'backend.patch'));
  assert.equal(sha256(backend), gui.backendPatchSha256, 'GUI snapshot backend patch differs');
  assert.equal(sha256(JSON.stringify({ files: gui.files, backendPatchSha256: gui.backendPatchSha256 })), gui.snapshotDigest,
    'GUI snapshot digest differs from manifest file inventory');
  const assets = gui.assets.map(asset => {
    const snapshotBytes = decodeHeader(read(safeChild(gui.snapshotPath, asset.header)).toString('utf8'));
    assert.equal(snapshotBytes.length, asset.bytes);
    assert.equal(sha256(snapshotBytes), asset.sha256);
    const cloneBytes = decodeHeader(read(safeChild(source, asset.header)).toString('utf8'));
    assert.deepEqual(cloneBytes, snapshotBytes, `Compiled GUI header differs: ${asset.header}`);
    const liveBytes = decodeHeader(read(safeChild(gui.sourcePath, asset.header)).toString('utf8'));
    assert.deepEqual(liveBytes, snapshotBytes, `Active GUI asset has advanced since this build: ${asset.header}`);
    const fullAddresses = embeddedLocations(fullSegments, snapshotBytes);
    assert.ok(fullAddresses.length, `Full linked firmware lacks GUI asset ${asset.header}`);
    for (const address of fullAddresses) assert.deepEqual(bytesAt(combined, address, snapshotBytes.length), snapshotBytes);
    return { header: asset.header, bytes: snapshotBytes.length, sha256: sha256(snapshotBytes),
      addresses: fullAddresses.map(hexAddress), byteExact: true, activeSourceHeaderMatches: true };
  });
  assert.equal(assets.length, 3, 'Menu, desktop and Help assets must be checked');
  const appliedValidation = gui.appliedSourceValidation.files.map(file => {
    assert.equal(sha256(read(safeChild(gui.appliedSourceValidation.path, file.path))), file.sha256,
      `Applied GUI validation snapshot differs: ${file.path}`);
    return { path: file.path, sha256: file.sha256, matches: true };
  });
  return { sourceHead: gui.sourceHead, sourcePath: gui.sourcePath, snapshotPath: gui.snapshotPath,
    selectedCommitMatches: true, selectedContentDigestMatches: true, maintainedSourceFilesClean: true,
    snapshotDigest: gui.snapshotDigest, snapshotFileCount: files.length, allSnapshotHashesMatch: true,
    allAppliedOverlayHashesMatch: true, files, assets, appliedSourceValidation: { passed: true, files: appliedValidation } };
}

const manifestPath = path.join(build, 'manifests/firmware-build.json');
for (const file of [manifestPath, nativeResultPath]) assert.ok(fs.existsSync(file), `Firmware build or native proof is not ready: ${file}`);
const manifest = json(manifestPath);
const currentProfile = manifest.buildProfile === firmwareVersion.releaseId;
assert.ok(currentProfile || ['native05', 'native05-exact', 'native06', 'native07', 'native08'].includes(manifest.buildProfile),
  `Unknown firmware build profile: ${manifest.buildProfile}`);
const expectedFilename = currentProfile ? firmwareVersion.filename : 'MHS-PowerEngine-TRPlus-v1_full.hex';
assert.equal(manifest.artifact, expectedFilename, 'Firmware artifact name differs from its release version');
const artifactPath = safeChild(path.join(build, 'firmware'), expectedFilename);
assert.ok(fs.existsSync(artifactPath), `Firmware artifact is not ready: ${artifactPath}`);
if (currentProfile) {
  assertGuiFirmwareVersion();
  assert.equal(manifest.mpeFirmwareVersion, firmwareVersion.version);
  assert.equal(manifest.firmwareFilename, firmwareVersion.filename);
  assert.equal(manifest.versionConfiguration.file, versionConfigurationPath);
  assert.equal(sha256(read(path.join(root, versionConfigurationPath))), manifest.versionConfiguration.sha256,
    'Firmware version configuration changed after the build');
}
const selectedGui = currentProfile ? {
  commit: firmwareVersion.gui.commit, snapshotDigest: firmwareVersion.gui.snapshotDigest
} : manifest.buildProfile === 'native08' ? {
  commit: 'ac4a5d6ce3d8037d4fdd7eee58899b9bc7463b3e',
  snapshotDigest: '3cba53dc478e6e69d6bc17a4cd243d2e8b3fa7a9f1778184fda78a0d552f10dd'
} : previousGui;
const extendedCartridge = currentProfile || ['native06', 'native07', 'native08'].includes(manifest.buildProfile);
assert.equal(path.resolve(manifest.sourcePath), path.resolve(source), 'Firmware manifest names a different source clone');
const artifact = read(artifactPath);
assert.equal(sha256(artifact), manifest.sha256, 'Combined full HEX differs from final build manifest hash');
assert.equal(artifact.length, manifest.bytes, 'Combined full HEX differs from final build manifest size');
const combined = hexSegments(artifactPath);
const images = {
  full: verifyImage('full', 'Source/Teensy/build', combined),
  minimalBoot: verifyImage('minimalBoot', 'Source/Teensy/MinimalBoot/build', combined)
};
assert.equal(images.minimalBoot.stackReserveBytes, manifest.minimalBootStackReserveBytes);
assert.equal(images.minimalBoot.ram2HeapReserveBytes, manifest.minimalBootRam2HeapReserveBytes);
assert.ok(manifest.minimalBootRam2MinimumHeapReserveBytes >= 262144);
const gui = verifyGui(manifest.customGui, combined, images.full._segments);
delete images.full._segments;
delete images.minimalBoot._segments;
const nativeResult = json(nativeResultPath);
const nativeModule = path.join(source, 'Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_MPE3TitlePull.c');
const nativeModuleSha256 = sha256(read(nativeModule));
assert.equal(path.resolve(nativeModule), path.resolve(nativeResult.nativeModule.path), 'Native proof used a different source clone');
assert.equal(nativeModuleSha256, nativeResult.nativeModule.sha256, 'Final native module differs from actual C++ harness module');
assert.equal(nativeResult.passed, true);
assert.equal(nativeResult.legacyIntro?.actualNativeModule, true);
assert.equal(nativeResult.legacyIntro?.endAcknowledged, true);
assert.equal(nativeResult.legacyIntro?.visits, 132);
assert.ok(nativeResult.sessionBytes > 0 && nativeResult.sessionBytes <= 65536, 'Native session exceeds the retired intro arena');
assert.equal(nativeResult.room, 2, 'Native proof did not reach gameplay Room 2');
assert.ok(nativeResult.nativeFrames > 0 && nativeResult.inputEvents >= 256, 'Native proof lacks gameplay frames or input sequence wrap');
if (currentProfile) {
  assert.equal(nativeResult.inputInterruptMasks, 0, 'Native input masked the PHI2 bus interrupt');
  assert.equal(nativeResult.pendingInputRejects, nativeResult.inputEvents, 'Native proof must reject a competing producer for every owned input snapshot');
  assert.ok(nativeResult.directionReversals >= 64, 'Native proof lacks repeated direction-change stress');
  for (const result of [nativeResult, ...(nativeResult.legacyFallback ? [nativeResult.legacyFallback] : [])]) {
    assert.equal(result.saveDirectory?.path, '/SAVES', 'Native proof lacks the dedicated save-directory implementation');
    assert.ok(result.saveDirectory.directoryChecks >= 5, 'Native proof lacks directory creation, existing directory, collision and failure checks');
    assert.ok(result.saveDirectory.fallbackChecks >= 8, 'Native proof lacks read-only folder/root restore ordering and legacy State fallback');
    assert.ok(result.saveDirectory.transactionFailureChecks >= 6, 'Native proof lacks failed writes/readback and save/backup promotion rollback');
    assert.equal(result.saveDirectory.rootWriteAttempts, 0, 'Native save attempted to write at the SD root');
    assert.equal(result.saveDirectory.rootMutationAttempts, 0, 'Native save attempted to remove or rename a legacy root file');
  }
}
assert.equal(nativeResult.storageChecks, 9, 'Native proof lacks the complete storage checks');
if (extendedCartridge) assert.equal(nativeResult.legacyStorageChecks, 6,
  'Extended cartridge proof lacks native05 save migration and rejection checks');
assert.equal(nativeResult.keyboardScanChecks, 4, 'Native proof lacks printable D/Z scan-pair regression');
assert.equal(nativeResult.pointerChecks, 8, 'Native proof lacks pointer envelope and dialog checks');
assert.equal(nativeResult.runtimeCpuEmulation, false);
assert.equal(sha256(read(options.raw)), nativeResult.rawSha256, 'Native proof used a different complete cartridge payload');
assert.equal(sha256(read(options.intro)), nativeResult.introSha256, 'Native proof used a different intro package');
assert.equal(sha256(read(nativeResult.wire.path)), nativeResult.wire.sha256, 'Exact native packet trace changed after proof');
const nativeInventoryPath = path.join(build, 'manifests/native-game-sources.json');
const inventory = json(nativeInventoryPath);
const expectedFiles = ['mpe4_game.h', 'mpe4_game.cpp', 'mpe4_package.h', 'mpe4_package.cpp',
  'mpe4_render.h', 'mpe4_render.cpp', 'mpe4_session.h', 'mpe4_session.cpp', 'mpe4_firmware.h'];
assert.deepEqual(inventory.map(entry => entry.file).sort(), [...expectedFiles].sort(), 'Native source inventory must contain all nine files exactly once');
assert.deepEqual(manifest.nativeGameSources, inventory, 'Final build manifest and native source inventory differ');
assert.deepEqual(nativeResult.nativeSources, inventory, 'Final linked source inventory differs from actual native C++ proof');
const nativeSources = inventory.map(entry => {
  assert.equal(sha256(read(safeChild(path.join(source, 'Source/Teensy/MinimalBoot/Common/NativeGame'), entry.file))), entry.sha256,
    `Native clone source changed since build: ${entry.file}`);
  assert.equal(sha256(read(safeChild(path.join(root, 'engine/native-game'), entry.file))), entry.sha256,
    `Canonical native source changed since build: ${entry.file}`);
  return { ...entry, cloneAndCanonicalMatch: true, matchesActualNativeHarness: true };
});
assert.match(read(path.join(source, 'Source/Teensy/MinimalBoot/Common/NativeGame/mpe4_session.h')).toString('utf8'),
  /static_assert\s*\(\s*sizeof\(Session\)\s*<=\s*65536/, 'ARM build must compile the complete native session arena guard');
const patches = manifest.patches.map(patch => {
  // Preserved native05 manifests predate the engine repository split.
  const recordedPath=patch.path.replaceAll('\\','/');
  const localPath=recordedPath.startsWith('teensyrom-plus/patches/')?
    'engine/patches/'+recordedPath.slice('teensyrom-plus/patches/'.length):recordedPath;
  assert.ok(localPath.startsWith('engine/patches/'),`Unexpected build patch path: ${patch.path}`);
  assert.equal(sha256(read(safeChild(root, localPath))), patch.sha256, `Build patch has changed: ${patch.path}`);
  return { ...patch, localPath, matches: true };
});
assert.ok(patches.some(patch => patch.path.includes('0033-Stream-native-intro-and-skip-to-login.patch')));
for (const prefix of ['0034-Publish-complete-frame-display-transitions', '0035-Run-native-SQ1-game-after-intro',
  '0036-Keep-cartridge-session-initialization-in-flash']) {
  assert.ok(patches.some(patch => path.basename(patch.path).startsWith(prefix)), `Missing final firmware patch ${prefix}`);
}
let nativeCartridge = null;
if (extendedCartridge) {
  const patch=patches.find(patch=>path.basename(patch.path).startsWith('0037-Stream-native-cartridges-up-to-four-MiB'));
  assert.ok(patch,'Extended cartridges require patch0037');
  execFileSync('git',['apply','--reverse','--check','--ignore-space-change',safeChild(root,patch.localPath)],
    {cwd:source,windowsHide:true,stdio:'pipe'});
  const header=path.join(source,'Source/Teensy/MinimalBoot/Common/MPE4Cartridge.h');
  nativeCartridge={patchApplied:true,headerSha256:sha256(read(header)),
    maximumPhysicalBytes:manifest.nativeGame.maximumPhysicalCartridgeBytes,
    maximumLogicalBytes:manifest.nativeGame.maximumLogicalCartridgeBytes};
  assert.equal(nativeCartridge.maximumPhysicalBytes,4194304);
  assert.equal(nativeCartridge.maximumLogicalBytes,4177920);
}
const verification = {
  schemaVersion: 1, verifiedAt: new Date().toISOString(),
  artifact: { file: artifactPath, sha256: manifest.sha256, bytes: artifact.length, matchesBuildManifest: true },
  buildManifest: { file: manifestPath, sha256: sha256(read(manifestPath)) },
  gui, images,
  nativeModule: { file: nativeModule, sha256: nativeModuleSha256, exactBytes: true,
    matchesActualNativeHarness: true, harnessResult: nativeResultPath, harnessResultSha256: sha256(read(nativeResultPath)) },
  nativeSources, nativeInventory: { file: nativeInventoryPath, sha256: sha256(read(nativeInventoryPath)),
    compileTimeArenaGuardPresent: true, hostSessionBytes: nativeResult.sessionBytes },
  nativeEvidence: { rawSha256: nativeResult.rawSha256, introSha256: nativeResult.introSha256,
    room: nativeResult.room, frames: nativeResult.nativeFrames, inputEvents: nativeResult.inputEvents,
    inputInterruptMasks: nativeResult.inputInterruptMasks ?? null, pendingInputRejects: nativeResult.pendingInputRejects ?? 0,
    directionReversals: nativeResult.directionReversals ?? 0,
    storageChecks: nativeResult.storageChecks, legacyStorageChecks: nativeResult.legacyStorageChecks ?? 0,
    saveDirectory: nativeResult.saveDirectory ?? null, packetTrace: nativeResult.wire },
  patches, nativeCartridge, physicalAcceptance: false,
  scope: 'Read-only combined HEX, both linked images, GUI snapshot and active headers, all native source hashes and actual firmware proof, linked FLASH methods, ITCM bus handlers and memory reserves; no build, flash, emulator or active-source mutation'
};
for (const [file, digest] of auditedInputs) {
  assert.equal(sha256(fs.readFileSync(file)), digest, `Audit input changed before verification completed: ${file}`);
}
verification.inputsUnchangedDuringAudit = { passed: true, files: auditedInputs.size };
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(verification, null, 2)}\n`);
console.log(JSON.stringify({ output, firmwareSha256: manifest.sha256, snapshotDigest: gui.snapshotDigest,
  guiAssets: gui.assets.map(({ header, sha256, bytes }) => ({ header, sha256, bytes })),
  nativeModuleSha256, images: Object.fromEntries(Object.entries(images).map(([name, image]) => [name, {
    embeddedBytes: image.embeddedBytes, stackReserveBytes: image.stackReserveBytes,
    ram2HeapReserveBytes: image.ram2HeapReserveBytes, itcmBytes: image.itcm.bytes
  }])) }, null, 2));
