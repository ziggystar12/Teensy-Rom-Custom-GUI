import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { firmwareVersion, versionConfigurationPath, assertGuiFirmwareVersion } from './firmware-version.mjs';

const root=path.resolve(import.meta.dirname,'..');
const options={build:null,release:null};
for(let i=2;i<process.argv.length;i+=2) {
  const key=process.argv[i].slice(2);
  assert.ok(key in options&&process.argv[i+1],`Unknown or incomplete ${process.argv[i]}`);
  options[key]=process.argv[i+1];
}
assert.ok(options.build&&/^native\d+$/.test(options.release??''),'--build DIR --release nativeNN are required');
assert.equal(options.release,firmwareVersion.releaseId,'Release id differs from firmware-version.json');
assertGuiFirmwareVersion();
const nativeReleaseNumber=Number.parseInt(options.release.slice('native'.length),10);
const build=path.resolve(options.build),destination=path.join(root,'releases',options.release);
assert.ok(!fs.existsSync(destination),'A published release directory is immutable; choose a new release id');
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const describe=(base,file)=>{const data=fs.readFileSync(path.join(base,file));return {file,sha256:hash(data),bytes:data.length};};
const checked=(file,expected)=>{const item=describe(root,file);assert.equal(item.sha256,expected,`Build input changed: ${file}`);return item;};
const proof=readJson(path.join(build,'manifests/firmware-build.json'));
assert.equal(proof.buildProfile,options.release,'Build profile and release id differ');
assert.equal(proof.mpeFirmwareVersion,firmwareVersion.version,'Public firmware version differs from the build');
assert.equal(proof.artifact,firmwareVersion.filename,'Firmware artifact filename differs from its public version');
assert.equal(proof.firmwareFilename,firmwareVersion.filename);
const versionConfiguration=checked(versionConfigurationPath,proof.versionConfiguration.sha256);
assert.ok(proof.minimalBootStackReserveBytes>=16384&&proof.minimalBootRam2HeapReserveBytes>=262144,'Firmware memory guards failed');
const guiRoot=firmwareVersion.gui.path;
const gui=readJson(path.join(root,guiRoot,'provenance.json'));
assert.equal(gui.sourceCommit,firmwareVersion.gui.commit,'Selected GUI commit differs from the release configuration');
assert.equal(gui.snapshotDigest,firmwareVersion.gui.snapshotDigest);
assert.equal(proof.customGui.sourceHead,gui.sourceCommit,'Selected GUI differs from the built GUI');
assert.equal(proof.customGui.snapshotDigest,gui.snapshotDigest);
const normalizedRelative=file=>file.replaceAll('\\','/');
const engineSources=proof.nativeGameSources.map(item=>checked(`engine/native-game/${normalizedRelative(item.file)}`,item.sha256));
assert.ok(Array.isArray(proof.nativeDosSources)&&proof.nativeDosSources.length>0,
  'Build proof is missing native DOS source provenance');
const nativeDosSources=proof.nativeDosSources.map(item=>checked(`engine/native-dos/${normalizedRelative(item.file)}`,item.sha256));
const patches=proof.patches.map(item=>checked(item.path,item.sha256));
if(options.release==='native18') {
  assert.equal(nativeDosSources.length,16,'native18 must record every compiled native DOS source');
  assert.equal(patches.length,45,'native18 must record patches 0001 through 0045');
}
let nativeRuntimeSources;
if(nativeReleaseNumber>=19) {
  assert.ok(proof.minimalBootRam2HeapReserveBytes>=327680,
    `${options.release} must retain at least 320 KiB of MinimalBoot RAM2 heap`);
  assert.ok(Array.isArray(proof.nativeRuntimeSources)&&proof.nativeRuntimeSources.length>0,
    `${options.release} build proof is missing native runtime source provenance`);
  nativeRuntimeSources=proof.nativeRuntimeSources.map(item=>
    checked(`engine/native-runtime/${normalizedRelative(item.file)}`,item.sha256));
  assert.deepEqual(nativeRuntimeSources.map(item=>item.file),['engine/native-runtime/mhs_native_arena.h'],
    `${options.release} must record the complete shared native runtime source inventory`);
  assert.equal(nativeDosSources.length,nativeReleaseNumber>=29 ? 21 : nativeReleaseNumber>=25 ? 20 : nativeReleaseNumber>=22 ? 19 : 16,
    `${options.release} must record every compiled native DOS source`);
  const requiredPatchCount=nativeReleaseNumber>=29 ? 50 : nativeReleaseNumber>=28 ? 49 : nativeReleaseNumber>=21 ? 47 : 46;
  assert.equal(patches.length,requiredPatchCount,
    `${options.release} must record patches 0001 through ${String(requiredPatchCount).padStart(4,'0')}`);
}
let nativeNesSources;
if(nativeReleaseNumber>=28) {
  assert.equal(proof.nativeNES?.cartridgeIdentity,'MHS NESVM',
    `${options.release} build proof is missing the NESVM service identity`);
  assert.deepEqual(proof.nativeNES?.mapperSupport,[0,11],
    `${options.release} build proof has unexpected NES mapper support`);
  assert.equal(proof.nativeNES?.memory,'Unused resident-cartridge RAM1 tail only; RAM2 untouched',
    `${options.release} must keep NESVM out of RAM2`);
  assert.ok(Array.isArray(proof.nativeNesSources)&&proof.nativeNesSources.length===12,
    `${options.release} build proof must record all twelve native NES source inputs`);
  nativeNesSources=proof.nativeNesSources.map(item=>
    checked(`engine/native-nes/${normalizedRelative(item.file)}`,item.sha256));
}
let nativeDoomSources;
if(nativeReleaseNumber>=29) {
  assert.equal(proof.nativeDoom?.cartridgeIdentity,'MHS DOOMVM',
    `${options.release} build proof is missing the DOOMVM service identity`);
  assert.equal(proof.nativeDoom?.ram2OverlayBytes,524288,
    `${options.release} must reserve all 512 KiB of RAM2 for DOOMVM`);
  assert.equal(proof.nativeDoom?.psramZoneBytes,8388608,
    `${options.release} must reserve the eight MiB PSRAM Doom zone`);
  assert.equal(proof.nativeDoom?.publicationReady,false,
    `${options.release} must preserve the upstream Doom publication gate`);
  assert.ok(Array.isArray(proof.nativeDoomSources)&&proof.nativeDoomSources.length===10,
    `${options.release} build proof must record all ten native Doom source inputs`);
  nativeDoomSources=proof.nativeDoomSources.map(item=>
    checked(`engine/native-doom/${normalizedRelative(item.file)}`,item.sha256));
}
const compiledVendorSources=proof.compiledVendorSources.map(item=>checked(`engine/vendor/vrEmu6502/${item.file}`,item.sha256));
const guiProvenance=checked(`${guiRoot}/provenance.json`,proof.customGui.sourceProvenanceSha256);
const guiBackend=checked('engine/custom-gui/backend.patch',proof.customGui.backendPatchSha256);
for(const file of gui.files) {
  const item=checked(`${guiRoot}/${file.path}`,file.sha256);assert.equal(item.bytes,file.bytes);
}
const artifactRoot=path.join(build,'firmware');
const names=[firmwareVersion.filename,'TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex','MHS-POWER-ENGINE.md'];
const files=names.map(file=>describe(artifactRoot,file));
assert.equal(files[0].sha256,proof.sha256);assert.equal(files[0].bytes,proof.bytes);
assert.equal(files[1].sha256,proof.officialRestoreSha256);
assert.equal(files[1].sha256,'575ab4e237b1c9d5539e8d56248490dd471c6e368d2c98fd66311dddb65252bf');
assert.equal(files[2].sha256,describe(root,'docs/FIRMWARE-GUIDE.md').sha256,'Guide changed after the build');
const manifest={schemaVersion:1,releaseId:options.release,mpeFirmwareVersion:firmwareVersion.version,
  firmwareFilename:firmwareVersion.filename,versionConfiguration,customGuiCommit:gui.sourceCommit,
  upstreamRepository:proof.upstream,upstreamCommit:proof.upstreamCommit,files,
  ...(nativeRuntimeSources?{nativeRuntimeSources}:{}),
  ...(nativeNesSources?{nativeNES:proof.nativeNES,nativeNesSources}:{}),
  ...(nativeDoomSources?{nativeDoom:proof.nativeDoom,nativeDoomSources}:{}),
  engineSources,nativeDosSources,patches,
  vendor:['.gitattributes','LICENSE','UPSTREAM.md','vrEmu6502.c','vrEmu6502.h'].map(file=>describe(root,`engine/vendor/vrEmu6502/${file}`)),
  buildTools:['scripts/build-firmware.ps1','scripts/prepare-teensyrom-custom-gui.mjs','scripts/create-native-release.mjs',
    'scripts/firmware-version.mjs','scripts/snapshot-custom-gui.mjs'].map(file=>describe(root,file)),
  gui:{sourceRepository:gui.sourceRepository,sourceCommit:gui.sourceCommit,snapshotDigest:gui.snapshotDigest,
    provenance:guiProvenance,backend:guiBackend,policy:describe(root,'engine/custom-gui/policy.json')},
  toolchain:{arduinoCli:proof.arduinoCliVersion,teensyCore:proof.teensyCoreVersion,crc32:proof.crc32LibraryVersion,
    acmeVersion:'0.97',acmeSha256:gui.assembler.sha256},
  memory:{minimalBootStackReserveBytes:proof.minimalBootStackReserveBytes,minimalBootRam2HeapReserveBytes:proof.minimalBootRam2HeapReserveBytes},
  cartridgeStorage:{source:'SD',maximumPhysicalBytes:4194304,maximumLogicalBytes:4177920,reservedBank:58,
    upperBanks:'64..255 are native resource pages; C64 EasyFlash bank decode remains 0..63'},
  scope:'MHS Power Engine firmware with the selected GUI and native AGI, DOSVM, NESVM, and DOOMVM launch services; cartridges and user game data are built separately.',
  buildProfile:proof.buildProfile,compiledVendorSources,
  vendorPlacement:'Retained upstream legacy dispatch table in DTCM, restored after historical patch0014'};
// Every input is checked before creating the new release. Never modify an old
// release or write a kit into the separate compiler repository implicitly.
fs.mkdirSync(destination,{recursive:true});
for(const file of files)fs.copyFileSync(path.join(artifactRoot,file.file),path.join(destination,file.file));
fs.writeFileSync(path.join(destination,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
fs.writeFileSync(path.join(destination,'SHA256SUMS.txt'),files.map(file=>`${file.sha256}  ${file.file}`).join('\n')+'\n');
console.log(JSON.stringify({release:destination,customGuiCommit:gui.sourceCommit,files},null,2));
