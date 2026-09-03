import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..');
const options={source:null,out:null,intro:null,raw:null,
  compiler:process.env.CXX??(process.platform==='win32'?'C:/msys64/mingw64/bin/g++.exe':'g++')};
if(process.argv.includes('--help')){console.log('node tests/run-mpe4-firmware-native-harness.mjs --source PATCHED_CLONE --out OUTPUT_DIR --intro M3T1.bin --raw CARTRIDGE.bin [--compiler g++]');process.exit(0);}
for(let i=2;i<process.argv.length;i+=2){const key=process.argv[i].slice(2);assert.ok(process.argv[i].startsWith('--')&&key in options&&process.argv[i+1]);options[key]=key==='compiler'?process.argv[i+1]:path.resolve(process.argv[i+1]);}
for(const key of ['source','out','intro','raw'])assert.ok(options[key],`--${key} is required`);
const native='Source/Teensy/MinimalBoot/Common/NativeGame',handlers='Source/Teensy/MinimalBoot/Common/IO_Handlers';
const files=['mpe4_game.h','mpe4_game.cpp','mpe4_package.h','mpe4_package.cpp','mpe4_render.h','mpe4_render.cpp','mpe4_session.h','mpe4_session.cpp','mpe4_firmware.h'];
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const nativeSources=files.map(file=>{const bytes=fs.readFileSync(path.join(options.source,native,file));assert.deepEqual(bytes,fs.readFileSync(path.join(root,'engine/native-game',file)),`Stale clone source: ${file}`);return {file,sha256:sha(bytes)};});
const modulePath=path.join(options.source,handlers,'IOH_MPE3TitlePull.c');
// Later integration patches legitimately edit the same title-service lines as
// patch 0035, so reversing that one intermediate patch is no longer a valid
// final-tree test. Verify its exact ordered build record and the live MPE4
// routing that this harness is about to compile and execute instead.
const buildProof=JSON.parse(fs.readFileSync(path.join(options.source,'..','manifests','firmware-build.json'),'utf8'));
const patchName='engine/patches/0035-Run-native-SQ1-game-after-intro.patch';
const patchIndex=buildProof.patches.findIndex(item=>item.path.replaceAll('\\','/')===patchName);
assert.equal(patchIndex,34,'Native game patch must remain number 0035 in the ordered build');
assert.equal(buildProof.patches[patchIndex].sha256,sha(fs.readFileSync(path.join(root,patchName))),'Build recorded a different native game patch');
const moduleText=fs.readFileSync(modulePath,'utf8');
assert.match(moduleText,/#include "\.\.\/NativeGame\/mpe4_firmware\.h"/);
assert.match(moduleText,/if \(MPE4Active\) \{ MPE4NextPacket\(\); return; \}/);
assert.match(moduleText,/if \(!MPE4Start\(\)\)[^]*?MPE3TitleFail\(0x40 \+ MPE4StartError\); return;/);
fs.mkdirSync(options.out,{recursive:true});
const exe=path.join(options.out,process.platform==='win32'?'firmware-native-harness.exe':'firmware-native-harness'),wire=path.join(options.out,'native-wire.bin');
execFileSync(options.compiler,['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation',...(process.platform==='win32'?['-static','-static-libgcc','-static-libstdc++']:[]),'-I',path.join(options.source,handlers),path.join(import.meta.dirname,'mpe4-firmware-native-harness.cpp'),'-o',exe],{cwd:path.isAbsolute(options.compiler)?path.dirname(options.compiler):root,windowsHide:true,stdio:'pipe',timeout:60000});
const output=execFileSync(exe,[options.intro,options.raw,wire],{windowsHide:true,encoding:'utf8',timeout:60000});
const raw=fs.readFileSync(options.raw);
const crc32=data=>{let crc=0xffffffff;for(const byte of data){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;};
const packOffset=raw.indexOf(Buffer.from('M4G1'));
assert.ok(packOffset>=0,'Native package header is required');
const packageHeader=Buffer.from(raw.subarray(packOffset,packOffset+64));
assert.equal(packageHeader.readUInt16LE(4),1);assert.equal(packageHeader.readUInt16LE(6),64);
const headerCrc=packageHeader.readUInt32LE(28);packageHeader.writeUInt32LE(0,28);assert.equal(crc32(packageHeader),headerCrc);
const packageFlags=packageHeader.readUInt32LE(32);
let legacyFallback=null;
if(packageFlags&2) {
  const legacyRaw=Buffer.from(raw),legacyPath=path.join(options.out,'legacy-bitmap-cart.bin'),legacyWire=path.join(options.out,'legacy-wire.bin');
  // Change only the terminal capability declaration and its header checksum.
  // The original package resource bytes/identity are unchanged, so this also
  // exercises save compatibility with the exact same game and state layout.
  packageHeader.writeUInt32LE(packageFlags&~0x302,32);packageHeader.writeUInt32LE(crc32(packageHeader),28);
  packageHeader.copy(legacyRaw,packOffset);fs.writeFileSync(legacyPath,legacyRaw);
  legacyFallback={...JSON.parse(execFileSync(exe,[options.intro,legacyPath,legacyWire],{windowsHide:true,encoding:'utf8',timeout:60000})),
    rawSha256:sha(legacyRaw),wire:{path:legacyWire,sha256:sha(fs.readFileSync(legacyWire))}};
  assert.equal(legacyFallback.spritesEnabled,false);assert.equal(legacyFallback.spritePackets,0);
}
const result={...JSON.parse(output),nativeModule:{path:modulePath,sha256:sha(fs.readFileSync(modulePath))},nativeSources,
  compiler:options.compiler,executableSha256:sha(fs.readFileSync(exe)),introSha256:sha(fs.readFileSync(options.intro)),rawSha256:sha(fs.readFileSync(options.raw)),
  wire:{path:wire,sha256:sha(fs.readFileSync(wire)),framing:'u16le length then exact M3 packet'},
  legacyFallback,
  scope:'Actual integrated firmware module with simulated SD and bus pins; deterministic conformance, not physical hardware timing.'};
fs.writeFileSync(path.join(options.out,'firmware-native-result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));
