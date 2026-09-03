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
  terminal: path.join(root, 'build/dos-work/dosvm-terminal.prg'),
  manifest: path.join(root, 'build/dos-work/dosvm-terminal.json'),
  wire: path.join(root, 'build/dos-work/dos-wire.bin'),
  font: path.join(root, 'build/dos-work/dos-font.bin'),
  text: path.join(root, 'build/dos-work/dos-screen.txt'),
  output: path.join(root, 'build/dos-work/dos-c64-wire-result.json'),
  'agi64-root': path.resolve(root, '../AGI-64')
};
if (process.argv.includes('--help')) {
  console.log('node dos/tests/mpe5_c64_wire_test.mjs [--terminal PRG] [--manifest JSON] [--wire BIN] [--font BIN] [--text TXT] [--output JSON] [--agi64-root PATH]');
  process.exit(0);
}
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index].slice(2);
  assert.ok(process.argv[index].startsWith('--') && key in options && process.argv[index + 1],
    `Unknown or incomplete option: ${process.argv[index]}`);
  options[key] = path.resolve(process.argv[index + 1]);
}
const planesOutput = path.join(path.dirname(options.output), 'dos-c64-planes.bin');
for (const input of ['terminal', 'manifest', 'wire', 'font', 'text']) {
  assert.notEqual(options.output, options[input], 'Replay output must not overwrite an input artifact');
  assert.notEqual(planesOutput, options[input], 'Plane output must not overwrite an input artifact');
}
assert.notEqual(planesOutput, options.output, 'JSON and plane outputs must use different paths');
// A rejected newer capture must not leave an old passing report beside it.
fs.rmSync(options.output, {force: true});
fs.rmSync(planesOutput, {force: true});
const importAgi = relative => import(pathToFileURL(path.join(options['agi64-root'], relative)).href);
const [{C64TerminalCpu}, {MPE3_TITLE_PULL: P, MPE3_TITLE_TERMINAL_STATE: T},
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
const wantedKeys = [
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
  }

  publish(cpu, ordinal) {
    this.ordinal = ordinal;
    const packet = packets[ordinal];
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
        if (!this.initialComplete) {
          assert.equal(this.initialSeen.has(cell), false, 'Firmware repeated an initial cell');
          this.initialSeen.add(cell);
          this.initialRecords++;
        }
      }
      if (packet[5] & P.cellFlagBaseComplete) {
        assert.equal(this.initialRecords, 1000);
        assert.equal(this.initialSeen.size, 1000);
        this.initialComplete = true;
        assert.equal(cpu.ram[T.baseReady], 1);
        assert.deepEqual(planes(cpu), this.expected, 'C64 did not present all 1,000 initial cells');
      }
    } else {
      assert.equal(this.initialComplete, true);
      assert.equal(packet[5], 0x25, 'DOS frame end must retain hires keyboard mode');
      assert.equal(packet[6], 26);
      assert.deepEqual(planes(cpu), this.expected, `C64 planes differ from firmware at frame ${this.frames}`);
      assert.equal(cpu.ram[K.active], 1, 'Validated DOS frame failed to activate keyboard');
      assert.equal(cpu.ram[0xd016], 8, 'DOS text unexpectedly entered multicolor mode');
      assert.equal(cpu.ram[0xd011], 0x3b, 'DOS bitmap is hidden');
      assert.equal(cpu.ram[0xd018], 0x78);
      assert.equal(cpu.ram[T.transitionHidden], 0);
      assert.equal(cpu.ram[T.parserSplit], 0);
      assert.deepEqual(Buffer.from(cpu.ram.subarray(0xd400, 0xd419)), Buffer.alloc(25));
      if (this.previousFrame?.equals(this.expected)) this.unchangedFrames++;
      this.previousFrame = Buffer.from(this.expected);
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
assert.equal(service.keys.length, 4, 'Terminal did not scan and emit DIR plus Return');
assert.ok(service.unchangedFrames >= 5, 'Wire lacks repeated idle prompt frame heartbeats');
assert.ok(cpu.irqCount > 0);
assert.equal(cpu.ram[K.active], 1);
assert.equal(cpu.ram[0xd016], 8);
assert.deepEqual(planes(cpu), service.expected);

// The publication regression exports this atlas only after checking every
// printable ASCII glyph for fallback, lowercase, and independent punctuation
// pixels. Compare every console character to the executed C64's real bitmap;
// sampling only BOULDER and C:\> previously missed comma-to-'?' corruption.
const font = fs.readFileSync(options.font);
assert.equal(font.length, 256 * 8, 'Run the publication regression to export the verified font');
const fontGlyph = character => font.subarray(character.charCodeAt(0) * 8, character.charCodeAt(0) * 8 + 8);
const commaGlyph = Buffer.from([0,0,0,0,0,0x30,0x30,0x60]);
assert.deepEqual(fontGlyph(','), commaGlyph, 'Verified font lacks the golden comma');
assert.deepEqual(fontGlyph('>'), Buffer.from([0x60,0x30,0x18,0x0c,0x18,0x30,0x60,0]),
  'Verified font has a reversed prompt arrow');
const finalPlanes = planes(cpu);
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
const result = {
  passed: true,
  proof: 'Exact generated DOS terminal executes integrated firmware wire in a deterministic 6510/CIA model; physical EasyFlash timing remains separate.',
  terminal: options.terminal,
  terminalSha256: sha256(prg),
  wire: options.wire,
  wireSha256: sha256(wire),
  packets: service.acks,
  completeFrames: service.frames,
  unchangedFrames: service.unchangedFrames,
  initialUniqueCells: service.initialSeen.size,
  inputEvents: service.keys,
  finalPlanes: planesOutput,
  finalPlanesSha256: sha256(finalPlanes),
  font: options.font,
  fontSha256: sha256(font),
  consoleText: options.text,
  consoleTextSha256: sha256(Buffer.from(sourceText)),
  verifiedConsoleCells: 1000,
  distinctCharacters: [...distinctCharacters].sort().join(''),
  visibleCommaCells: commaCells,
  visibleLowercaseCells: lowercaseCells,
  videoMode: {width: 320, height: 200, multicolor: false,
    d011: cpu.ram[0xd011], d016: cpu.ram[0xd016], d018: cpu.ram[0xd018]},
  directoryEntryVisible: true,
  returnedPromptVisible: true,
  hires: true,
  keyboardActive: true,
  instructions: cpu.instructions,
  rasterInterrupts: cpu.irqCount
};
fs.writeFileSync(planesOutput, finalPlanes);
fs.writeFileSync(options.output, `${JSON.stringify(result, null, 2)}\n`);
console.log(`MPE5 C64 wire replay passed: ${service.acks} firmware packets, ${service.frames} 320x200 hires frames, all 1,000 console characters including ${commaCells} commas, returned C:\\> prompt, and DIR/Return keyboard events.`);
