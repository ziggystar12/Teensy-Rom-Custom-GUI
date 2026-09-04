#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const source=path.resolve(process.argv[2]??'');
if(!source||!fs.existsSync(path.join(source,'.git')))throw new Error('Pass the applied TeensyROM source checkout');
const read=relative=>fs.readFileSync(path.join(source,relative),'utf8');
const parser=read('Source/Teensy/FileParsers.ino');
const cartridge=read('Source/Teensy/MinimalBoot/Common/MPE4Cartridge.h');
const title=read('Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_MPE3TitlePull.c');
const nes=read('Source/Teensy/MinimalBoot/Common/NativeNES/mpe6_firmware.h');
const cpu=read('Source/Teensy/MinimalBoot/Common/NativeNES/vendor/chips/m6502.h');
const minimal=read('Source/Teensy/MinimalBoot/Min_TeensyROM.h');

assert.match(parser,/static const char NesName\[32\] = "MHS NESVM";/);
assert.match(cartridge,/static const char nesName\[32\][^\n]*="MHS NESVM";/);
assert.match(title,/#include "\.\.\/NativeNES\/mpe6_firmware\.h"/);
assert.ok(title.indexOf('!memcmp(Magic, "N6D1", 4)')<title.indexOf('!memcmp(Magic, "M5D1", 4)'),
  'NES descriptor must be probed before the generic title header');
assert.match(title,/Data == 3 && MPE6Active[^\n]*MPE6LatchInput/);
assert.match(title,/if \(MPE6Active\) \{ MPE6NextPacket\(\); return; \}/);
assert.match(title,/else if \(MPE6Active\) MPE6PumpPending\(\)/);
assert.match(title,/MPE6ResumeAfterACK\(\)/);

assert.match(nes,/SD\.sdfs\.open\("\/NESVM\/ROMS",O_RDONLY\)/);
assert.match(nes,/file\.fileSize\(\)!=entry\.bytes/);
assert.match(nes,/MPE6HashFile\(path,entry\.bytes,digest\)/);
assert.match(nes,/memcmp\(digest,loadedDigest,sizeof\(digest\)\)/);
assert.match(nes,/pressed&\(nes::A\|nes::Start\)/);
assert.match(nes,/buttons&\(nes::Start\|nes::Select\)/);
assert.match(nes,/MPE6DisplayState=1/);
assert.match(nes,/void \*menuStorage=MPE6Take/);
assert.match(cpu,/M6502_CODE uint64_t m6502_tick\(/);
assert.match(minimal,/#define AGIStackDeduction\s+64/);
assert.doesNotMatch(nes,/MPE5BorrowResetOnlyArena|MPE5QuiesceRam2Services|MPE5_RAM2_BASE|MHSNativeArenaClaim|DMAMEM/,
  'NESVM must not reserve or overwrite DOSVM RAM2');

console.log('NESVM firmware integration PASS: exact launcher, folder selection, unchanged-load revalidation, controls, ACK pumping, and RAM2 isolation.');
