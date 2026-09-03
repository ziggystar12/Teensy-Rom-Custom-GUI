#!/usr/bin/env node
// Replay the exact integrated firmware wire through the exact generated DOS
// 6510 terminal. The CPU/CIAs execute; the mailbox and raster are deterministic
// service models, so this proves protocol/display/input behavior, not bus timing.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const options = {
  scenario: 'text',
  terminal: path.join(root, 'build/dos-work/dosvm-terminal.prg'),
  manifest: path.join(root, 'build/dos-work/dosvm-terminal.json'),
  wire: path.join(root, 'build/dos-work/dos-wire.bin'),
  font: path.join(root, 'build/dos-work/dos-font.bin'),
  text: path.join(root, 'build/dos-work/dos-screen.txt'),
  output: path.join(root, 'build/dos-work/dos-c64-wire-result.json'),
  'expected-planes': null,
  frame: null,
  'agi64-root': path.resolve(root, '../AGI-64')
};
if (process.argv.includes('--help')) {
  console.log('node dos/tests/mpe5_c64_wire_test.mjs [--scenario text|graphics] [--terminal PRG] [--manifest JSON] [--wire BIN] [--font BIN] [--text TXT] [--expected-planes BIN] [--frame JSON] [--output JSON] [--agi64-root PATH]');
  process.exit(0);
}
const supplied = new Set();
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index].slice(2);
  assert.ok(process.argv[index].startsWith('--') && key in options && process.argv[index + 1],
    `Unknown or incomplete option: ${process.argv[index]}`);
  options[key] = key === 'scenario' ? process.argv[index + 1] : path.resolve(process.argv[index + 1]);
  supplied.add(key);
}
assert.ok(['text', 'graphics'].includes(options.scenario), 'Scenario must be text or graphics');
const graphics = options.scenario === 'graphics';
if (graphics) {
  if (!supplied.has('wire')) options.wire = path.join(root, 'build/dos-work/boulder-wire.bin');
  if (!supplied.has('output')) options.output = path.join(root, 'build/dos-work/boulder-c64-wire-result.json');
}
const planesOutput = path.join(path.dirname(options.output), graphics ? 'boulder-c64-planes.bin' : 'dos-c64-planes.bin');
for (const input of ['terminal', 'manifest', 'wire', 'font', 'text', 'expected-planes', 'frame']) {
  if (!options[input]) continue;
  assert.notEqual(options.output, options[input], 'Replay output must not overwrite an input artifact');
  assert.notEqual(planesOutput, options[input], 'Plane output must not overwrite an input artifact');
}
assert.notEqual(planesOutput, options.output, 'JSON and plane outputs must use different paths');
// A rejected newer capture must not leave an old passing report beside it.
fs.rmSync(options.output, {force: true});
fs.rmSync(planesOutput, {force: true});
const importAgi = relative => import(pathToFileURL(path.join(options['agi64-root'], relative)).href);
const [{C64TerminalCpu, isPlaneAddress}, {MPE3_TITLE_PULL: P, MPE3_TITLE_TERMINAL_STATE: T},
  {MPE4_INPUT: K}, {crc16Ccitt}] = await Promise.all([
  importAgi('test/helpers/c64-terminal-cpu.mjs'),
  importAgi('host/mpe3-title-terminal.mjs'),
  importAgi('host/mpe4-keyboard.mjs'),
  importAgi('host/save-disk.mjs')
]);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const manifest = JSON.parse(fs.readFileSync(options.manifest, 'utf8'));
const prg = fs.readFileSync(options.terminal);
assert.equal(manifest.format, 'M3TP-DOSVM-terminal');
assert.equal(manifest.gameplay, true);
assert.equal(manifest.enable1351Mouse, false);
assert.equal(sha256(prg), manifest.terminalPrgSha256, 'Terminal PRG differs from its generated manifest');
assert.equal(sha256(fs.readFileSync(path.join(options['agi64-root'], 'host/mpe3-title-terminal.mjs'))),
  manifest.agi64TerminalSourceSha256, 'Terminal state definitions differ from the generated artifact');
if (manifest.dosTerminalOverlaySha256) {
  assert.equal(sha256(fs.readFileSync(path.join(root, 'dos/tools/dos_terminal.mjs'))),
    manifest.dosTerminalOverlaySha256, 'DOS terminal overlay differs from the generated artifact');
  assert.equal(manifest.dosSidPayloadBytes, 27);
}
if (graphics) assert.ok(manifest.dosTerminalOverlaySha256, 'Graphics replay requires the DOS background extension');
const program = {prg, labels: manifest.labels, stageAddress: manifest.stageAddress};
for (const label of ['entry', 'apply_cells', 'apply_cells_ok', 'terminal_error_hold', 'sample_game_input'])
  assert.ok(Number.isInteger(program.labels?.[label]), `Terminal manifest lacks ${label}`);

const wire = fs.readFileSync(options.wire);
const packets = [];
for (let cursor = 0; cursor < wire.length;) {
  assert.ok(cursor + 2 <= wire.length, 'Truncated wire length');
  const length = wire.readUInt16LE(cursor); cursor += 2;
  assert.ok(length >= 10 && length <= 238 && cursor + length <= wire.length, 'Invalid wire extent');
  const packet = wire.subarray(cursor, cursor + length); cursor += length;
  assert.equal(packet.subarray(0, 2).toString('ascii'), 'M3');
  assert.equal(packet[2], P.protocolVersion);
  assert.equal(length, packet[6] + 10);
  assert.equal(packet.readUInt16LE(length - 2), crc16Ccitt(packet.subarray(0, length - 2)), 'Firmware packet CRC');
  assert.ok(packet[3] === P.packetCell || packet[3] === P.packetSid, 'Unexpected DOS packet type');
  const previousSequence = packets.at(-1)?.[4] ?? 0;
  assert.equal(packet[4], previousSequence === 255 ? 1 : previousSequence + 1, 'Firmware packet sequence');
  packets.push(packet);
}
assert.ok(packets.length > 54, 'Wire must include an initial screen and later DOS frames');
assert.equal(packets.at(-1)[3], P.packetSid, 'Capture must end at a complete DOS frame');

const C = {command: 0xdff4, status: 0xdff5, ack: 0xdff6, commit: 0xdff7};
const wantedKeys = graphics ? [] : [
  {ascii: 68, scan: 32, row: 2, column: 2, shift: true},
  {ascii: 73, scan: 23, row: 4, column: 1, shift: true},
  {ascii: 82, scan: 19, row: 2, column: 1, shift: true},
  {ascii: 13, scan: 28, row: 0, column: 1, shift: false}
];

function planes(cpu) {
  return Buffer.concat([
    Buffer.from(cpu.ram.subarray(0x6000, 0x6000 + 8000)),
    Buffer.from(cpu.ram.subarray(0x5c00, 0x5c00 + 1000)),
    Buffer.from(cpu.ram.subarray(0xd800, 0xd800 + 1000))
  ]);
}

function visiblePixels(data, hires, background) {
  const cellWidth = hires ? 8 : 4, width = cellWidth * 40;
  const pixels = Buffer.alloc(width * 200);
  for (let cell = 0; cell < 1000; cell++) {
    const screen = data[8000 + cell];
    const colours = hires ? [screen & 15, screen >>> 4] :
      [background, screen >>> 4, screen & 15, data[9000 + cell] & 15];
    for (let row = 0; row < 8; row++) {
      const bits = data[cell * 8 + row];
      const start = (Math.floor(cell / 40) * 8 + row) * width + (cell % 40) * cellWidth;
      for (let column = 0; column < cellWidth; column++)
        pixels[start + column] = colours[hires ? (bits >>> (7 - column)) & 1 :
          (bits >>> (6 - column * 2)) & 3];
    }
  }
  return pixels;
}

class FirmwareWireService {
  constructor() {
    this.started = false;
    this.ordinal = -1;
    this.acks = 0;
    this.frames = 0;
    this.expected = Buffer.alloc(10000);
    this.initialSeen = new Set();
    this.initialRecords = 0;
    this.initialComplete = false;
    this.keys = [];
    this.releaseKey = false;
    this.previousFrame = null;
    this.unchangedFrames = 0;
    this.lastHires = true;
    this.lastBackground = 0;
    this.multicolorFrames = 0;
    this.distinctMulticolorFrames = new Set();
    this.audibleFrames = 0;
    this.lastAudible = false;
    this.gateOnTransitions = 0;
    this.gateOffTransitions = 0;
    this.sidStates = new Set();
    this.replacement = null;
    this.replacementsShown = 0;
    this.hiddenCellWrites = 0;
    this.hiddenAtIrq = null;
  }

  publish(cpu, ordinal) {
    this.ordinal = ordinal;
    const packet = packets[ordinal];
    if (this.initialComplete && packet[3] === P.packetCell &&
        ((packet[5] & P.cellFlagReplace) ||
         (!this.replacement && (packet[5] & P.cellFlagModeValid) &&
          Boolean(packet[5] & P.cellFlagHires) !== this.lastHires))) {
      // A newer full replacement may supersede a still-hidden one. Every
      // displayed replacement must nevertheless contain all 1,000 cells.
      this.replacement = {seen: new Set(), hires: Boolean(packet[5] & P.cellFlagHires), shown: false};
    }
    cpu.ram.fill(0, P.dataAddress, P.controlAddress);
    cpu.ram.set(packet, P.dataAddress);
    cpu.ram[C.status] = 2;
    cpu.ram[C.commit] = packet[4];
  }

  onRead(cpu, address) {
    if (address !== program.labels.sample_game_input) return;
    cpu.controls.matrix.fill(0);
    if (!cpu.ram[K.armed]) return; // Let the real release scan arm input.
    if (this.releaseKey) { this.releaseKey = false; return; }
    const key = wantedKeys[this.keys.length];
    if (!key) return;
    cpu.controls.matrix[key.row] = 1 << key.column;
    if (key.shift) cpu.controls.matrix[1] |= 0x80;
  }

  onWrite(cpu, address, value) {
    if (this.started && this.initialComplete) {
      const pending = packets[this.ordinal];
      if (address === 0xd011) {
        if (!(value & 0x10)) this.hiddenAtIrq = cpu.irqCount;
        else if (this.replacement) {
          assert.equal(pending[3], P.packetSid, 'Replacement became visible before its frame-end packet');
          assert.equal(this.replacement.seen.size, 1000, 'Partial replacement became visible');
          assert.deepEqual(planes(cpu), this.expected, 'Replacement became visible with incomplete planes');
          assert.equal(cpu.ram[0xd016], this.replacement.hires ? 8 : 0x18,
            'Replacement became visible in the previous display mode');
          assert.equal(cpu.ram[0xd021], pending[6] === 27 ? pending[34] & 15 : 0,
            'Replacement became visible with the previous background colour');
          this.replacement.shown = true;
        }
      }
      if (this.replacement && isPlaneAddress(address) &&
          cpu.pc >= program.labels.apply_cells && cpu.pc < program.labels.apply_cells_ok) {
        assert.equal(cpu.ram[0xd011] & 0x10, 0, 'Partial replacement cell was written with display enabled');
        assert.equal(cpu.ram[T.transitionHidden], 1);
        assert.ok(this.hiddenAtIrq !== null && cpu.irqCount > this.hiddenAtIrq,
          'Replacement changed pixels before reaching the hidden border');
        this.hiddenCellWrites++;
      }
    }
    if (address === C.command && value) {
      if (value === P.commandStart) {
        assert.equal(this.started, false, 'Terminal tried to restart the firmware');
        assert.equal(Buffer.from(cpu.ram.subarray(0xdff0, 0xdff4)).toString('ascii'), 'M3TP');
        assert.equal(cpu.ram[P.bankAddress], P.helperBank);
        assert.equal(cpu.ram[0xdff8] | cpu.ram[0xdff9] << 8 | cpu.ram[0xdffa] << 16, P.assetRaw);
        this.started = true;
        cpu.ram.fill(0, 0xdffc, 0xe000); // MPE5Start resets keyboard controls.
        this.publish(cpu, 0);
      } else if (value === K.command) {
        assert.equal(this.initialComplete, true, 'Keyboard sent input before complete base image');
        assert.ok(this.frames > 0, 'Keyboard sent input before gameplay frame activation');
        const key = wantedKeys[this.keys.length];
        assert.ok(key, 'Unexpected duplicate keyboard event');
        const event = [K.keyRegister, K.scanRegister, K.joyRegister, K.flagsRegister,
          K.sequenceRegister, K.checksumRegister].map(register => cpu.ram[register]);
        const sequence = this.keys.length + 1;
        assert.deepEqual(event, [key.ascii, key.scan, 0, 1, sequence,
          0xa5 ^ key.ascii ^ key.scan ^ 1 ^ sequence], 'C64 keyboard differs from MPE5 input envelope');
        this.keys.push(event);
        cpu.ram[K.ack] = sequence;
        this.releaseKey = true;
      } else assert.fail(`Unexpected DOS terminal command ${value}`);
    }
    if (address !== C.ack || !this.started) return;
    const packet = packets[this.ordinal];
    assert.equal(value, packet[4], 'Terminal ACK differs from pending firmware sequence');
    if (packet[3] === P.packetCell) {
      assert.equal(packet[6] % P.cellRecordBytes, 0);
      if (!this.initialComplete && (packet[5] & P.cellFlagReplace)) {
        // Exclude the transport canary when the actual initial frame begins.
        this.initialSeen.clear();
        this.initialRecords = 0;
      }
      for (let offset = 8; offset < 8 + packet[6]; offset += P.cellRecordBytes) {
        const cell = packet.readUInt16LE(offset);
        assert.ok(cell < 1000);
        packet.copy(this.expected, cell * 8, offset + 2, offset + 10);
        this.expected[8000 + cell] = packet[offset + 10];
        this.expected[9000 + cell] = packet[offset + 11] & 15;
        if (this.replacement) {
          assert.equal(Boolean(packet[5] & P.cellFlagHires), this.replacement.hires);
          this.replacement.seen.add(cell);
        }
        if (!this.initialComplete) {
          assert.equal(this.initialSeen.has(cell), false, 'Firmware repeated an initial cell');
          this.initialSeen.add(cell);
          this.initialRecords++;
        }
      }
      if (!this.initialComplete && (packet[5] & P.cellFlagBaseComplete)) {
        assert.equal(this.initialRecords, 1000);
        assert.equal(this.initialSeen.size, 1000);
        this.initialComplete = true;
        assert.equal(cpu.ram[T.baseReady], 1);
        assert.deepEqual(planes(cpu), this.expected, 'C64 did not present all 1,000 initial cells');
      }
      if (this.replacement) {
        assert.equal(cpu.ram[0xd011] & 0x10, 0, 'Replacement became visible at CELL acknowledgment');
        assert.equal(cpu.ram[T.transitionHidden], 1);
      }
    } else {
      assert.equal(this.initialComplete, true);
      const hires = Boolean(packet[5] & P.cellFlagHires);
      if (!graphics) assert.equal(hires, true, 'DOS text unexpectedly entered multicolor mode');
      assert.equal(packet[5] & ~P.cellFlagHires, 0x21, 'DOS frame end must retain full-screen keyboard mode');
      assert.ok(packet[6] === 26 || packet[6] === 27, 'Invalid DOS SID/video payload length');
      if (packet[6] === 27) assert.equal(manifest.dosSidPayloadBytes, 27);
      const background = packet[6] === 27 ? packet[34] & 15 : 0;
      const sid = packet.subarray(9, 34);
      assert.deepEqual(planes(cpu), this.expected, `C64 planes differ from firmware at frame ${this.frames}`);
      assert.equal(cpu.ram[K.active], 1, 'Validated DOS frame failed to activate keyboard');
      assert.equal(cpu.ram[0xd016], hires ? 8 : 0x18, 'Frame mode differs from the firmware hires flag');
      assert.equal(cpu.ram[0xd021], background, 'Frame background differs from the DOS video payload');
      assert.equal(cpu.ram[0xd011], 0x3b, 'DOS bitmap is hidden');
      assert.equal(cpu.ram[0xd018], 0x78);
      assert.equal(cpu.ram[T.transitionHidden], 0);
      assert.equal(cpu.ram[T.parserSplit], 0);
      assert.deepEqual(Buffer.from(cpu.ram.subarray(0xd400, 0xd419)), sid,
        'C64 SID registers differ from the firmware speaker payload');
      this.sidStates.add(sid.toString('hex'));
      const audible = Boolean((sid[24] & 15) && [4, 11, 18].some(register => sid[register] & 1));
      if (audible) this.audibleFrames++;
      if (audible !== this.lastAudible) {
        if (audible) this.gateOnTransitions++;
        else this.gateOffTransitions++;
      }
      this.lastAudible = audible;
      if (this.previousFrame?.equals(this.expected) && hires === this.lastHires &&
          background === this.lastBackground) this.unchangedFrames++;
      if (!hires) {
        this.multicolorFrames++;
        this.distinctMulticolorFrames.add(sha256(visiblePixels(this.expected, false, background)));
      }
      if (this.replacement) {
        assert.equal(this.replacement.seen.size, 1000, 'Firmware ended an incomplete replacement');
        assert.equal(this.replacement.shown, true, 'Complete replacement remained hidden');
        this.replacementsShown++;
        this.replacement = null;
      }
      this.previousFrame = Buffer.from(this.expected);
      this.lastHires = hires;
      this.lastBackground = background;
      this.frames++;
    }
    this.acks++;
    if (this.ordinal + 1 < packets.length) this.publish(cpu, this.ordinal + 1);
  }
}

const service = new FirmwareWireService();
const cpu = new C64TerminalCpu(program, service, {recordWrites: false});
cpu.runUntil(machine => service.acks === packets.length || machine.pc === program.labels.terminal_error_hold,
  Math.max(5_000_000, packets.length * 50_000));
assert.notEqual(cpu.pc, program.labels.terminal_error_hold,
  `C64 terminal error ${cpu.ram[T.error]} after ${service.acks} packets`);
assert.equal(cpu.ram[T.error], 0);
assert.equal(service.acks, packets.length);
assert.equal(service.keys.length, wantedKeys.length, 'Terminal did not emit the expected keyboard events');
if (!graphics) assert.ok(service.unchangedFrames >= 5, 'Wire lacks repeated idle prompt frame heartbeats');
assert.ok(cpu.irqCount > 0);
assert.equal(cpu.ram[K.active], 1);
assert.equal(cpu.ram[0xd016], service.lastHires ? 8 : 0x18);
assert.deepEqual(planes(cpu), service.expected);
const finalPlanes = planes(cpu);
const visibleColours = [...new Set(visiblePixels(finalPlanes, service.lastHires, service.lastBackground))].sort((a,b) => a-b);
if (graphics) {
  assert.ok(service.multicolorFrames >= 2, 'Graphics wire lacks multiple visible multicolor frames');
  assert.ok(service.distinctMulticolorFrames.size >= 2, 'Graphics wire never changes visible multicolor pixels');
  assert.ok(service.replacementsShown >= 1 && service.hiddenCellWrites >= 10000,
    'Graphics wire did not exercise a complete hidden display replacement');
  assert.ok(visibleColours.length >= 2, 'Final graphics frame has no visible colour contrast');
}
let expectedPlanesHash = null, expectedFrame = null;
if (options['expected-planes']) {
  const expectedPlanes = fs.readFileSync(options['expected-planes']);
  assert.equal(expectedPlanes.length, 10000, 'Firmware capture must contain bitmap, screen and colour planes');
  assert.deepEqual(finalPlanes, expectedPlanes, 'Final C64 planes differ from the independent firmware capture');
  expectedPlanesHash = sha256(expectedPlanes);
}
if (options.frame) {
  expectedFrame = JSON.parse(fs.readFileSync(options.frame, 'utf8'));
  assert.equal(typeof expectedFrame.hires, 'boolean');
  assert.ok(Number.isInteger(expectedFrame.background) && expectedFrame.background >= 0 && expectedFrame.background < 16);
  assert.equal(service.lastHires, expectedFrame.hires, 'Final C64 mode differs from captured firmware metadata');
  assert.equal(service.lastBackground, expectedFrame.background, 'Final C64 background differs from captured firmware metadata');
  if (expectedFrame.audibleFrames > 0) {
    assert.ok(service.audibleFrames > 0 && service.gateOnTransitions > 0,
      'Captured speaker activity never reached the C64 SID');
    assert.ok(service.gateOffTransitions > 0,
      'Captured speaker activity never returned the C64 SID to silence');
  }
}

// The publication regression exports this atlas only after checking every
// printable ASCII glyph for fallback, lowercase, and independent punctuation
// pixels. Compare every console character to the executed C64's real bitmap;
// sampling only BOULDER and C:\> previously missed comma-to-'?' corruption.
function verifyConsole() {
const font = fs.readFileSync(options.font);
assert.equal(font.length, 256 * 8, 'Run the publication regression to export the verified font');
const fontGlyph = character => font.subarray(character.charCodeAt(0) * 8, character.charCodeAt(0) * 8 + 8);
const commaGlyph = Buffer.from([0,0,0,0,0,0x30,0x30,0x60]);
assert.deepEqual(fontGlyph(','), commaGlyph, 'Verified font lacks the golden comma');
assert.deepEqual(fontGlyph('>'), Buffer.from([0x60,0x30,0x18,0x0c,0x18,0x30,0x60,0]),
  'Verified font has a reversed prompt arrow');
const sourceText = fs.readFileSync(options.text, 'utf8');
const lines = sourceText.replace(/\r\n?/g, '\n').split('\n');
if (lines.at(-1) === '') lines.pop();
assert.equal(lines.length, 25, 'Native console capture must have 25 rows');
let commaCells = 0, lowercaseCells = 0;
const distinctCharacters = new Set();
for (let row = 0; row < 25; row++) {
  assert.equal(lines[row].length, 40, `Console row ${row + 1} must have 40 characters`);
  for (let column = 0; column < 40; column++) {
    const character = lines[row][column];
    assert.ok(character >= ' ' && character <= '~', 'Console text must be printable ASCII');
    const glyph = fontGlyph(character);
    if (character !== '?') assert.notDeepEqual(glyph, fontGlyph('?'),
      `Console character ${JSON.stringify(character)} falls back to '?'`);
    const cell = row * 40 + column;
    assert.deepEqual(finalPlanes.subarray(cell * 8, cell * 8 + 8), glyph,
      `C64 row ${row + 1}, column ${column + 1} does not display ${JSON.stringify(character)}`);
    if (character !== ' ')
      assert.notEqual(finalPlanes[8000 + cell] >>> 4, finalPlanes[8000 + cell] & 15,
        `C64 row ${row + 1}, column ${column + 1} has invisible foreground`);
    if (character === ',') {
      assert.deepEqual(finalPlanes.subarray(cell * 8, cell * 8 + 8), commaGlyph);
      commaCells++;
    }
    if (character >= 'a' && character <= 'z') lowercaseCells++;
    distinctCharacters.add(character);
  }
}
assert.ok(commaCells > 0, 'DIR capture must include visible comma punctuation');
assert.ok(lowercaseCells > 0, 'Console capture must exercise lowercase text');
function visibleText(text, {lineStart = false, blankTail = false} = {}) {
  for (let start = 0; start < 1000; start++) {
    if ((lineStart && start % 40 !== 0) || start % 40 + text.length > 40) continue;
    if (![...text].every((character, index) => {
      const cell = start + index;
      return fontGlyph(character).equals(finalPlanes.subarray(cell * 8, cell * 8 + 8)) &&
        (character === ' ' || finalPlanes[8000 + cell] >>> 4);
    })) continue;
    if (blankTail) {
      let blank = true;
      for (let cell = start + text.length; cell % 40; cell++) {
        if ((finalPlanes[8000 + cell] >>> 4) !== (finalPlanes[8000 + cell] & 15) &&
            finalPlanes.subarray(cell * 8, cell * 8 + 8).some(byte => byte)) blank = false;
      }
      if (!blank) continue;
    }
    return true;
  }
  return false;
}
assert.ok(visibleText('BOULDER  EXE'), 'Final C64 bitmap does not show the BOULDER.EXE directory entry');
assert.ok(visibleText('C:\\>', {lineStart: true, blankTail: true}),
  'Final C64 bitmap lacks a clean returned C:\\> prompt');
return {
  font: options.font,
  fontSha256: sha256(font),
  consoleText: options.text,
  consoleTextSha256: sha256(Buffer.from(sourceText)),
  verifiedConsoleCells: 1000,
  distinctCharacters: [...distinctCharacters].sort().join(''),
  visibleCommaCells: commaCells,
  visibleLowercaseCells: lowercaseCells,
  directoryEntryVisible: true,
  returnedPromptVisible: true
};
}
const consoleProof = graphics ? {} : verifyConsole();
const result = {
  passed: true,
  scenario: options.scenario,
  proof: 'Exact generated DOS terminal executes integrated firmware wire in a deterministic 6510/CIA model; physical EasyFlash timing remains separate.',
  terminal: options.terminal,
  terminalSha256: sha256(prg),
  wire: options.wire,
  wireSha256: sha256(wire),
  packets: service.acks,
  completeFrames: service.frames,
  unchangedFrames: service.unchangedFrames,
  multicolorFrames: service.multicolorFrames,
  distinctMulticolorFrames: service.distinctMulticolorFrames.size,
  completeReplacementsShown: service.replacementsShown,
  hiddenReplacementCellWrites: service.hiddenCellWrites,
  sidRegisterFramesVerified: service.frames,
  distinctSidStates: service.sidStates.size,
  audibleSidFrames: service.audibleFrames,
  speakerGateOnTransitions: service.gateOnTransitions,
  speakerGateOffTransitions: service.gateOffTransitions,
  initialUniqueCells: service.initialSeen.size,
  inputEvents: service.keys,
  finalPlanes: planesOutput,
  finalPlanesSha256: sha256(finalPlanes),
  expectedPlanes: options['expected-planes'],
  expectedPlanesSha256: expectedPlanesHash,
  expectedFrameMetadata: options.frame,
  capturedFrame: expectedFrame,
  visibleColours,
  ...consoleProof,
  videoMode: {width: 320, logicalWidth: service.lastHires ? 320 : 160, height: 200, multicolor: !service.lastHires,
    background: service.lastBackground, d011: cpu.ram[0xd011], d016: cpu.ram[0xd016], d018: cpu.ram[0xd018]},
  hires: service.lastHires,
  keyboardActive: true,
  instructions: cpu.instructions,
  rasterInterrupts: cpu.irqCount
};
fs.writeFileSync(planesOutput, finalPlanes);
fs.writeFileSync(options.output, `${JSON.stringify(result, null, 2)}\n`);
console.log(graphics ?
  `MPE5 C64 graphics replay passed: ${service.acks} packets, ${service.multicolorFrames} multicolor frames (${service.distinctMulticolorFrames.size} visibly distinct), ${service.replacementsShown} complete hidden replacements, and ${service.frames} exact SID register frames.` :
  `MPE5 C64 wire replay passed: ${service.acks} firmware packets, ${service.frames} hires frames, all 1,000 console characters including ${consoleProof.visibleCommaCells} commas, returned C:\\> prompt, and DIR/Return keyboard events.`);
