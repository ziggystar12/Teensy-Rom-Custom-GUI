#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const sourceArgument = process.argv[2];
if (!sourceArgument) {
  throw new Error('Usage: node doom/tests/mpe7_firmware_source_test.mjs <staged-TeensyROM-source>');
}
const source = path.resolve(sourceArgument);
const read = relative => {
  const file = path.join(source, relative);
  if (!fs.statSync(file, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`Missing staged TeensyROM file: ${relative}`);
  }
  return fs.readFileSync(file, 'utf8').replaceAll('\r\n', '\n');
};
const between = (text, start, end) => {
  const first = text.indexOf(start);
  const last = text.indexOf(end, first + start.length);
  assert.ok(first >= 0 && last > first, `Unable to isolate ${start}`);
  return text.slice(first, last);
};
const ordered = (text, needles, message) => {
  let cursor = -1;
  for (const needle of needles) {
    const next = text.indexOf(needle, cursor + 1);
    assert.ok(next > cursor, `${message}: missing or out of order: ${needle}`);
    cursor = next;
  }
};

const parser = read('Source/Teensy/FileParsers.ino');
const cartridge = read('Source/Teensy/MinimalBoot/Common/MPE4Cartridge.h');
const title = read('Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_MPE3TitlePull.c');
const doom = read('Source/Teensy/MinimalBoot/Common/NativeDoom/mpe7_firmware.h');
const arena = read('Source/Teensy/MinimalBoot/Common/NativeRuntime/mhs_native_arena.h');
const minimal = read('Source/Teensy/MinimalBoot/MinimalBoot.ino');
const commonDefs = read('Source/Teensy/MinimalBoot/Common/Common_Defs.h');
const lower = read('Source/Teensy/tools/BootLinkerFiles/imxrt1062_t41.ld.orig');
const upper = read('Source/Teensy/tools/BootLinkerFiles/imxrt1062_t41.ld.upper');
const upperBoot = read('Source/Teensy/tools/BootLinkerFiles/bootdata.c.upper');

// Exact full-firmware and MinimalBoot cartridge identities.
for (const header of ['mhs_native_adapter.h', 'mpe7_target.h', 'mpe_doom_session.h']) {
  assert.match(doom, new RegExp(`#include "\\.\\.\\/\\.\\.\\/${header.replace('.', '\\.') }"`),
    `nested firmware header must reach ${header} at the Arduino sketch root`);
}
assert.match(parser, /static const char DoomName\[32\]\s*=\s*"MHS DOOMVM";/);
assert.match(parser, /memcmp\(Header \+ 0x20, DoomName, sizeof\(DoomName\)\) == 0/);
assert.match(cartridge, /static const char doomName\[32\][^\n]*="MHS DOOMVM";/);
assert.match(cartridge, /!memcmp\(h\+32,doomName,32\)/);
assert.match(title, /#include "\.\.\/NativeDoom\/mpe7_firmware\.h"/);
ordered(title, ['!memcmp(Magic, "M7D1", 4)', 'MPE7Start(Root)',
  '!memcmp(Magic, "N6D1", 4)', '!memcmp(Magic, "M5D1", 4)'],
  'M7D1 dispatch must precede the existing native descriptor probes');
assert.match(title, /return MPE7Start\(Root\) \? 0\s*:\s*\(MPE7Error \? MPE7Error : MPE3TitleErrorHeader\);/);

// IO2 input, immutable-packet production, ACK, and pending-packet pumping.
assert.match(title, /\(\(MPE4Active \|\| MPE5Active \|\| MPE6Active \|\| MPE7Active\)\s*&&\s*Address >= 0xFD\)/);
assert.match(title, /Data == 3 && MPE7Active\) \{ MPE7LatchInput\(\); return; \}/);
assert.match(title, /if \(MPE7Active \|\| MPE7Ram2Owned\) \{ MPE7NextPacket\(\); return; \}/);
assert.match(title, /if \(MPE7Active \|\| MPE7Ram2Owned\)\s*\{\s*MPE7ResumeAfterACK\(\);\s*return;\s*\}/);
assert.match(title, /else if \(MPE7Active \|\| MPE7Ram2Owned\) MPE7PumpPending\(\);/);

const latch = between(doom, 'static inline void MPE7LatchInput()',
  'static FLASHMEM void MPE7AppendInputEdge');
assert.match(latch, /!\(flags & 0x80u\) \|\| \(flags & \(uint8_t\)~0x8fu\)/);
assert.match(latch, /0xa5u \^ key \^ scan \^ joy \^ flags \^ sequence/);
assert.match(latch, /MPE7InputPending = true;/);
assert.doesNotMatch(latch, /MPE3TitleMailbox\[0xfcu\]\s*=/,
  'the ISR must not ACK input before foreground Session acceptance');
const applyInput = between(doom, 'static FLASHMEM bool MPE7ApplyInput()',
  'static FLASHMEM void MPE7Pump()');
ordered(applyInput, ['pendingScanEvents() + count', 'updateInput(update)',
  'MPE3TitleMailbox[0xfcu] = input.sequence', 'MPE7InputPending = false'],
  'foreground input must capacity-check, apply, then ACK');

const nextPacket = between(doom, 'static FLASHMEM void MPE7NextPacket()',
  'static FLASHMEM void MPE7ResumeAfterACK()');
ordered(nextPacket, ['MPE7Pump();', 'MPE7Session->changes(',
  'MPE3TitlePublish(MPE3TitleCELL', 'MPE7PublishSid();'],
  'MPE7 packet production order');
assert.match(doom, /memset\(MPE3TitlePacket \+ MPE3TitlePacketHeaderBytes, 0, 26u\);[\s\S]*MPE3TitlePublish\(MPE3TitleSID, 0x21u, 26u\);/);
const resume = between(doom, 'static FLASHMEM void MPE7ResumeAfterACK()',
  'static FLASHMEM void MPE7PumpPending()');
ordered(resume, ['PendingType == MPE3TitleCELL', 'MPE7FrameEndSidPending = true',
  'PendingType == MPE3TitleSID', 'MPE7Session->acknowledgeFrameEnd()',
  'MPE7FrameEndSidPending = false'], 'frame end must complete only after SID ACK');
assert.match(doom, /static FLASHMEM void MPE7PumpPending\(\)\s*\{[\s\S]*MPE7Pump\(\);\s*\}/);

// Descriptor and linked-layout gates: exactly 8 MiB PSRAM and all 512 KiB RAM2.
for (const pattern of [
  /MPE7Protocol = 1u;/,
  /MPE7DescriptorBytes = 16u;/,
  /MPE7RequiredPsramMiB = 8u;/,
  /MPE7RequiredRam2Blocks = 64u;/,
  /MPE7Ram2Base = 0x20200000u;/,
  /MPE7Ram2Bytes = 512u \* 1024u;/,
  /MPE7ZoneBase = 0x70000000u;/,
  /MPE7ZoneBytes = 8u \* 1024u \* 1024u;/,
  /memcmp\(descriptor, "M7D1", 4u\)/,
  /descriptor\[4\] != MPE7Protocol/,
  /descriptor\[5\] != MPE7DescriptorBytes/,
  /descriptor\[6\] != MPE7RequiredPsramMiB/,
  /descriptor\[7\] != MPE7RequiredRam2Blocks/,
  /external_psram_size < MPE7RequiredPsramMiB/,
  /zoneStart != MPE7ZoneBase \|\| zoneEnd != MPE7ZoneBase \+ MPE7ZoneBytes/,
  /dataStart != MPE7Ram2Base/,
  /runtimeEnd != MPE7Ram2Limit/
]) assert.match(doom, pattern);

// The takeover is ordered and irreversible; no normal allocator survives it.
assert.match(arena, /DOS,\s*Doom/);
assert.match(doom, /if \(MPE7Ram2Owned \|\| MHSNativeArenaRequiresReset\(\)\) return;/);
ordered(doom, ['MPE7TargetPrepare(MPE7WadPath', 'AGIPictureReleaseSource();',
  'MHSNativeArenaClaim(MPE7ArenaOwner', 'if (myFile) myFile.close();',
  'free(BigBuf)', 'MPE5QuiesceRam2Services()',
  'MHSNativeArenaSealResetOnly(MPE7ArenaOwner)', 'MPE7Ram2Owned = true;',
  'MPE7InitializeCoreOverlay();', 'MPE7TargetBeginClaimed(MPE7EmuArena',
  'MPE7Session->start('], 'reset-only RAM2 handoff');
assert.match(doom,
  /static MHSNativeArenaView MPE7ArenaView __attribute__\(\(used\)\);/,
  'the linked release must retain the accepted Doom arena lease in RAM1');
assert.match(title, /if \(MPE5Ram2Owned \|\| MPE7Ram2Owned\) \{ REBOOT; return true; \}/);
assert.match(minimal, /if \(MPE5Ram2Owned \|\| MPE7Ram2Owned\) \{ REBOOT; return; \}/);
assert.match(minimal, /if \(!MPE5Ram2Owned && !MPE7Ram2Owned && Serial\.available\(\)\)/);

// Deterministic lower/upper firmware split and MPE7 linker-owned regions.
assert.match(commonDefs, /^\s*#define\s+UpperAddr\s+0x180000\b/m);
assert.match(lower, /FLASH \(rwx\): ORIGIN = 0x60000000, LENGTH = 1536K/);
assert.match(lower, /RAM \(rwx\):\s+ORIGIN = 0x20200000, LENGTH = 512K/);
assert.match(lower, /ERAM \(rwx\):\s+ORIGIN = 0x70000000, LENGTH = 16384K/);
assert.match(upper, /FLASH \(rwx\): ORIGIN = 0x60180000, LENGTH = 6400K/);
assert.match(upperBoot, /^\s*0x60180000,\s*$/m);
assert.match(lower, /\/\* MPE7_DOOM_LINKER_LAYOUT_V1 \*\//);
assert.match(lower, /OVERLAY ORIGIN\(RAM\) : NOCROSSREFS/);
assert.match(lower, /\.mpe7\.ram\s*\{/);
assert.ok(lower.indexOf('*mhsdoom_core_*.o(.text ') < lower.indexOf('.text.itcm'),
  'Doom code must route to flash before the broad ITCM collector');
for (const coldSection of [
  '.text._vfprintf_r',
  '.text._svfprintf_r',
  '.text.__ssvfscanf_r',
  '.text._vfiprintf_r',
  '.text._strtod_l',
  '.text._dtoa_r',
]) {
  assert.ok(lower.indexOf(coldSection) < lower.indexOf('.text.itcm'),
    `${coldSection} must stay XIP so Doom does not consume a fourth ITCM bank`);
}
assert.equal(lower.match(/\*mhsdoom_core_\*\.o\(/g)?.length, 5,
  'lower linker must contain the five exact Doom core collectors');
ordered(lower, ['__mpe7_data_start = .;', '__mpe7_data_end = .;',
  '__mpe7_bss_start = .;', '__mpe7_bss_end = .;',
  '__mpe7_runtime_start = .;', '__mpe7_data_load = LOADADDR(.mpe7.ram);',
  '__mpe7_runtime_end = ORIGIN(RAM) + LENGTH(RAM);'],
  'MPE7 RAM2 linker symbols');
ordered(lower, ['PROVIDE(MemPool = ORIGIN(ERAM));',
  '__mpe7_zone_start = ORIGIN(ERAM);',
  '__mpe7_zone_end = __mpe7_zone_start + 0x00800000;'],
  'MPE7 PSRAM linker symbols');
for (const assertion of [
  'ASSERT(__mpe7_bss_end <= ORIGIN(RAM) + LENGTH(RAM)',
  'ASSERT(__mpe7_runtime_end - __mpe7_runtime_start >= 0x00020000',
  'ASSERT(MemPool == ORIGIN(ERAM)',
  'ASSERT(__mpe7_zone_end <= ORIGIN(ERAM) + LENGTH(ERAM)',
  'ASSERT(__text_csf_end <= ORIGIN(FLASH) + LENGTH(FLASH)',
  'ASSERT(_itcm_block_count <= 4',
  'ASSERT(_estack >= _ebss + 0x00004000',
  '_flashimagelen = __text_csf_end - ORIGIN(FLASH);'
]) assert.ok(lower.includes(assertion), `Missing lower-linker gate: ${assertion}`);
assert.doesNotMatch(`${commonDefs}\n${lower}\n${upper}\n${upperBoot}`,
  /UpperAddr\s+0x060000|LENGTH = 7936K|0x60060000|LENGTH = 7552K/,
  'legacy firmware boundaries must not survive Doom staging');

console.log('DOOMVM firmware integration PASS: exact M7D1 identity, input/packet/ACK pumping, reset-only 512 KiB RAM2 ownership, 8 MiB PSRAM, and bounded lower/upper firmware layout.');
