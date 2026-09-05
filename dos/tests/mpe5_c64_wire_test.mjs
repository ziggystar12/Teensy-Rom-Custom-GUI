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
  'agi64-root': path.resolve(root, 'vm/client')
};
if (process.argv.includes('--help')) {
  console.log('node dos/tests/mpe5_c64_wire_test.mjs [--scenario text|graphics|input] [--terminal PRG] [--manifest JSON] [--wire BIN] [--font BIN] [--text TXT] [--expected-planes BIN] [--frame JSON] [--output JSON] [--agi64-root PATH]');
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
assert.ok(['text', 'graphics', 'input'].includes(options.scenario), 'Scenario must be text, graphics, or input');
const graphics = options.scenario === 'graphics';
const inputOnly = options.scenario === 'input';
if (inputOnly && !supplied.has('output')) options.output = path.join(root, 'build/dos-work/dos-c64-input-result.json');
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
if (!inputOnly) fs.rmSync(planesOutput, {force: true});
const importAgi = relative => import(pathToFileURL(path.join(options['agi64-root'], relative)).href);
const [{C64TerminalCpu, isPlaneAddress}, {MPE3_TITLE_PULL: P, MPE3_TITLE_TERMINAL_STATE: T},
  {MPE4_INPUT: K}, {crc16Ccitt}] = await Promise.all([
  import(pathToFileURL(path.join(root, 'vm/tests/helpers/c64-terminal-cpu.mjs')).href),
  importAgi('host/mpe3-title-terminal.mjs'),
  importAgi('host/mpe4-keyboard.mjs'),
  import('../../vm/tests/helpers/crc16.mjs')
]);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const badPathGlyph = [0, 3, 2, 7, 2, 2, 7];
const pathSeparator = Buffer.from([0x80,0x80,0x40,0x40,0x20,0x20,0x10,0x10]);
function repairPackedPathSeparator(bitmap, offset) {
  for (const shift of [4, 0]) {
    const mask = shift ? 0xf0 : 0x0f;
    if (!badPathGlyph.every((value, row) => ((bitmap[offset + row] & mask) >>> shift) === value)) continue;
    for (let row = 0; row < 8; row++)
      bitmap[offset + row] = (bitmap[offset + row] & (mask ^ 0xff)) |
        (pathSeparator[row] >>> (4 - shift));
  }
}
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
assert.equal(manifest.dosInputProtocol, 'held-scan-v2-ctrl-alt-del');
assert.equal(manifest.dosTextCompatibility, 'v1017-backslash-v1');
assert.equal(sha256(fs.readFileSync(path.join(options['agi64-root'], 'host/mpe4-keyboard.mjs'))),
  manifest.agi64KeyboardSourceSha256, 'Keyboard tables differ from the generated terminal');
if (graphics) assert.ok(manifest.dosTerminalOverlaySha256, 'Graphics replay requires the DOS background extension');
const program = {prg, labels: manifest.labels, stageAddress: manifest.stageAddress};
for (const label of ['entry', 'apply_cells', 'apply_cells_ok', 'terminal_error_hold', 'sample_game_input'])
  assert.ok(Number.isInteger(program.labels?.[label]), `Terminal manifest lacks ${label}`);

// Execute the actual emitted routine against the CIA switch/joystick model.
// This is separate from the display replay so held keys and delayed ACKs can
// span deterministic input polls without tying tests to captured frame count.
function checkKeyboard() {
  const events = [];
  let maximumCaptureInstructions = 0;
  let acknowledge = true;
  const service = {onWrite(cpu, address, value) {
    if (address !== 0xdff4 || value !== K.command) return;
    const event = [K.keyRegister, K.scanRegister, K.joyRegister, K.flagsRegister,
      K.sequenceRegister, K.checksumRegister].map(register => cpu.ram[register]);
    assert.equal(event[5], 0xa5 ^ event[0] ^ event[1] ^ event[2] ^ event[3] ^ event[4]);
    events.push(event);
    if (acknowledge) cpu.ram[K.ack] = event[4];
  }};
  const cpu = new C64TerminalCpu(program, service, {rasterInterruptPeriod: 0, recordWrites: false});
  function call(label) {
    const sentinel = 0x0200;
    cpu.push((sentinel - 1) >>> 8); cpu.push((sentinel - 1) & 255);
    cpu.pc = program.labels[label]; cpu.runUntil(c => c.pc === sentinel, 10_000);
  }
  const ticks = T.rasterTicks;
  function poll(advance = 1) {
    cpu.ram[ticks] = (cpu.ram[ticks] + advance) & 255;
    const count = events.length;
    if (program.labels.dos_capture_input !== undefined) {
      const before = cpu.instructions; call('dos_capture_input');
      maximumCaptureInstructions = Math.max(maximumCaptureInstructions, cpu.instructions - before);
    }
    call('sample_game_input'); return events.slice(count);
  }
  function controls({keys = [], joy = 0, port1 = 0} = {}) {
    cpu.controls.matrix.fill(0); cpu.controls.port2Bits = joy; cpu.controls.port1Bits = port1;
    for (const [row, column] of keys) cpu.controls.matrix[row] |= 1 << column;
  }
  function state(expected, held, description) {
    controls(held);
    const emitted = poll();
    assert.equal(emitted.length, 1, `${description}: expected one state transition`);
    assert.deepEqual(emitted[0].slice(0, 4), expected, description);
    const stable = poll(); assert.equal(stable.length, 0, `${description}: held state repeated too early`);
  }
  function release() { state([0, 0, 0, 0x80], {}, 'All keys and joystick released'); }
  call('game_input_init');
  assert.equal(poll().length, 0); assert.equal(cpu.ram[K.armed], 1);
  state([0, 77, 0, 0x80], {keys: [[0, 2]]}, 'Cursor Right'); release();
  state([0, 75, 0, 0x80], {keys: [[0, 2], [1, 7]]}, 'Shift+Cursor Left consumes C64 Shift'); release();
  state([0, 80, 0, 0x80], {keys: [[0, 7]]}, 'Cursor Down'); release();
  state([0, 72, 0, 0x80], {keys: [[0, 7], [6, 4]]}, 'Right Shift+Cursor Up consumes C64 Shift'); release();
  state([0, 0, 0, 0x81], {keys: [[1, 7]]}, 'Left Shift alone');
  state([65, 30, 0, 0x81], {keys: [[1, 7], [1, 2]]}, 'Shift+A retains modifier');
  state([97, 30, 0, 0x80], {keys: [[1, 2]]}, 'Release Shift while A remains down'); release();
  state([0, 0, 0, 0x81], {keys: [[6, 4]]}, 'Right Shift alone'); release();
  state([0, 0, 0, 0x82], {keys: [[7, 2]]}, 'Control alone');
  state([3, 46, 0, 0x82], {keys: [[7, 2], [2, 4]]}, 'Control+C'); release();
  state([0, 0, 0, 0x84], {keys: [[7, 5]]}, 'Commodore maps Alt'); release();
  state([0, 83, 0, 0x86], {keys: [[0, 0], [7, 2], [7, 5]]},
    'Ctrl+Commodore+Delete maps PC Ctrl+Alt+Delete');
  state([0, 83, 0, 0x80], {keys: [[0, 0]]},
    'Reboot Delete remains latched while modifiers release');
  release();
  state([0, 65, 0, 0x86], {keys: [[7, 2], [7, 5], [0, 3]]},
    'Ctrl+Commodore+F7 display shortcut reaches firmware'); release();
  for (const [column, scan] of [[4, 59], [5, 61], [6, 63], [3, 65]]) {
    state([0, scan, 0, 0x80], {keys: [[0, column]]}, `F${scan - 58}`); release();
    state([0, scan + 1, 0, 0x80], {keys: [[0, column], [1, 7]]}, `F${scan - 57} consumes C64 Shift`); release();
  }
  for (const joy of [1, 2, 4, 8, 1 | 4, 16]) {
    state([0, 0, joy, 0x80], {joy}, `Port 2 joystick ${joy}`); release();
  }
  controls({port1: 31}); assert.equal(poll().length, 0, 'Port 1 grounds must not invent DOS keys');
  controls();
  state([8, 14, 0, 0x80], {keys: [[0, 0]]}, 'Backspace make');
  assert.equal(poll(18).length, 0, 'Typematic must wait 20 raster ticks');
  assert.deepEqual(poll()[0].slice(0, 4), [8, 14, 0, 0x88], 'Backspace typematic repeat');
  assert.equal(poll(3).length, 0);
  assert.deepEqual(poll()[0].slice(0, 4), [8, 14, 0, 0x88], 'Subsequent repeat waits four ticks'); release();
  // Delayed firmware ACK: retransmit exactly the immutable pending state,
  // then deliver a physical release as soon as that state is accepted.
  acknowledge = false; controls({keys: [[0, 2]]});
  const make = poll()[0]; controls();
  assert.deepEqual(poll()[0], make, 'Pending make packet was mutated before ACK');
  cpu.ram[K.ack] = make[4]; acknowledge = true;
  assert.deepEqual(poll()[0].slice(0, 4), [0, 0, 0, 0x80], 'Release lost behind pending ACK');
  assert.equal(poll().length, 0, 'Released cursor emitted a duplicate state');
  assert.ok(events.every(event => event[0] !== 27 && event[1] !== 1),
    'A cursor, modifier, or joystick input invented Escape');
  state([27, 1, 0, 0x80], {keys: [[7, 7]]}, 'Run/Stop intentionally maps Escape'); release();
  const mappingEvents = events.length;
  if (program.labels.dos_capture_input !== undefined) {
    // Exhaust the bounded FIFO with ACK stopped. After it resumes, the final
    // physical release must converge even when that release did not fit.
    acknowledge = false; controls({keys: [[1, 7]]});
    const first = poll()[0], overflowStart = events.length - 1;
    for (let index = 0; index < 40; index++) {
      controls(index & 1 ? {} : {keys: [[1, 2]]}); poll();
    }
    controls(); acknowledge = true; cpu.ram[K.ack] = first[4];
    for (let index = 0; index < 40; index++) poll();
    const accepted = [...new Map(events.slice(overflowStart).map(event => [event[4], event])).values()];
    const expected = [[0, 0, 0, 0x81], ...Array.from({length: 31}, (_, index) =>
      index & 1 ? [0, 0, 0, 0x80] : [97, 30, 0, 0x80]), [0, 0, 0, 0x80]];
    assert.deepEqual(accepted.map(event => event.slice(0, 4)), expected,
      'Input FIFO overflow lost the final release or damaged queued states');
    assert.equal(poll().length, 0, 'Overflow recovery emitted a duplicate release');

    // The new raster capture must preserve interrupted transfer registers,
    // flags, stack position, and all shared zero-page transfer pointers.
    const sentinel = 0x0240, flags = 0x21;
    const pointers = Buffer.from(Array.from({length: 16}, (_, index) => 0xa0 + index));
    cpu.ram.set(pointers, 0xf0); cpu.a = 0x5a; cpu.x = 0xa7; cpu.y = 0x3c;
    const stack = cpu.sp;
    cpu.push(sentinel >>> 8); cpu.push(sentinel & 255); cpu.push(flags);
    cpu.p = flags | 4; cpu.pc = program.labels.raster_irq;
    cpu.runUntil(c => c.pc === sentinel, 10_000);
    assert.deepEqual([cpu.a, cpu.x, cpu.y, cpu.p, cpu.sp], [0x5a, 0xa7, 0x3c, flags, stack],
      'Raster input capture damaged interrupted CPU state');
    assert.deepEqual(Buffer.from(cpu.ram.subarray(0xf0, 0x100)), pointers,
      'Raster input capture damaged transfer pointers');
  }
  return {events: mappingEvents, instructions: cpu.instructions, protocol: manifest.dosInputProtocol,
    arrows: true, shift: true, control: true, alt: true, functionKeys: 'F1-F8', port2Joystick: true,
    releases: true, delayedAck: true, typematic: true, noSpuriousEscape: true,
    fifoCapacity: 31, overflowReleaseRecovery: true, irqPreservesCpu: true, maximumCaptureInstructions};
}
// Real input lasts a bounded number of raster frames; it does not wait for a
// test to acknowledge it. Execute the foreground loop and raster IRQ together
// while each firmware input acknowledgement is delayed twelve video frames.
// This is a cadence/ordering model, not a cycle-accurate cartridge benchmark.
function checkTimedKeyboard() {
  const events = [];
  let pending = null, acknowledgeAt = Infinity;
  const service = {
    onRead(cpu) {
      if (pending && cpu.irqCount >= acknowledgeAt) {
        cpu.ram[K.ack] = pending[4]; pending = null; acknowledgeAt = Infinity;
      }
    },
    onWrite(cpu, address, value) {
      if (address !== 0xdff4 || value !== K.command) return;
      const event = [K.keyRegister, K.scanRegister, K.joyRegister, K.flagsRegister,
        K.sequenceRegister, K.checksumRegister].map(register => cpu.ram[register]);
      assert.equal(event[5], 0xa5 ^ event[0] ^ event[1] ^ event[2] ^ event[3] ^ event[4]);
      if (pending) assert.deepEqual(event, pending, 'Timed input changed an unacknowledged packet');
      else {
        // ACK can land after the foreground read but before its retransmit.
        if (event[4] === events.at(-1)?.[4]) {
          assert.deepEqual(event, events.at(-1), 'Late retransmit mutated an accepted snapshot');
          return;
        }
        events.push(event); pending = event; acknowledgeAt = cpu.irqCount + 12;
      }
    }
  };
  const cpu = new C64TerminalCpu(program, service, {rasterInterruptPeriod: 0, recordWrites: false});
  const loop = 0x0200, initReturn = 0x0240;
  cpu.push((initReturn - 1) >>> 8); cpu.push((initReturn - 1) & 255);
  cpu.pc = program.labels.game_input_init;
  cpu.runUntil(c => c.pc === initReturn, 10_000);
  cpu.ram.set([0x20, program.labels.sample_game_input & 255,
    program.labels.sample_game_input >>> 8, 0x4c, loop & 255, loop >>> 8], loop);
  cpu.pc = loop; cpu.ram[0xfffe] = program.labels.raster_irq & 255;
  cpu.ram[0xffff] = program.labels.raster_irq >>> 8;
  cpu.ram[0xd01a] = 1; cpu.p &= ~4; cpu.rasterInterruptPeriod = 6000;
  const taps = [
    {start: 2, end: 5, row: 1, column: 5, ascii: 115, scan: 31},
    {start: 6, end: 9, row: 1, column: 2, ascii: 97, scan: 30},
    {start: 10, end: 13, row: 3, column: 4, ascii: 98, scan: 48}
  ];
  while (cpu.irqCount < 90) {
    cpu.controls.matrix.fill(0);
    const tap = taps.find(key => cpu.irqCount >= key.start && cpu.irqCount < key.end);
    if (tap) cpu.controls.matrix[tap.row] = 1 << tap.column;
    cpu.step();
  }
  const expected = taps.flatMap(key => [[key.ascii, key.scan, 0, 0x80], [0, 0, 0, 0x80]]);
  assert.deepEqual(events.map(event => event.slice(0, 4)), expected,
    'Three-frame key taps were lost while firmware ACK was delayed twelve frames');
  return {tapFrames: 3, acknowledgementFrames: 12, taps: taps.length, snapshots: events.length,
    rasterCapture: true, pendingPayloadImmutable: true};
}
const inputProof = {...checkKeyboard(), timed: checkTimedKeyboard()};
if (inputOnly) {
  fs.writeFileSync(options.output, `${JSON.stringify(inputProof, null, 2)}\n`);
  console.log(`MPE5 C64 input passed: ${inputProof.events} snapshots, arrows, Shift/Ctrl/Alt, F1-F8, port 2, releases, repeat, and delayed ACK.`);
  process.exit(0);
}

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
const expectedFrame = options.frame ? JSON.parse(fs.readFileSync(options.frame, 'utf8')) : null;
const scroll = expectedFrame?.scroll;
if (scroll) {
  assert.ok(graphics && Number.isInteger(scroll.firstPacket) && scroll.firstPacket > 0);
  assert.ok(Number.isInteger(scroll.packetCount) && scroll.packetCount > 0 &&
    scroll.firstPacket + scroll.packetCount <= packets.length);
  assert.ok(scroll.stateChanges >= 2 && scroll.startAddressAfter !== scroll.startAddressBefore,
    'Boulder capture did not exercise actual CRTC scrolling');
}

const C = {command: 0xdff4, status: 0xdff5, ack: 0xdff6, commit: 0xdff7};
const wantedKeys = graphics ? [] : [
  {ascii: 68, scan: 32, row: 2, column: 2, shift: true, flags: 0x81},
  {ascii: 73, scan: 23, row: 4, column: 1, shift: true, flags: 0x81},
  {ascii: 82, scan: 19, row: 2, column: 1, shift: true, flags: 0x81},
  {ascii: 13, scan: 28, row: 0, column: 1, shift: false, flags: 0x80}
].flatMap(key => [key, {ascii: 0, scan: 0, flags: 0x80}]);

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
    this.visibleScrollPackets = 0;
    this.sharpHiresFrames = 0;
    this.sharpColorFrames = 0;
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
    const key = wantedKeys[this.keys.length];
    if (!key) return;
    if (key.row !== undefined) cpu.controls.matrix[key.row] = 1 << key.column;
    if (key.shift) cpu.controls.matrix[1] |= 0x80;
  }

  onWrite(cpu, address, value) {
    const scrolling = scroll && this.ordinal >= scroll.firstPacket &&
      this.ordinal < scroll.firstPacket + scroll.packetCount;
    if (scrolling && address === 0xd011)
      assert.ok(value & 0x10, 'Boulder scrolling disabled the C64 display');
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
        assert.deepEqual(event, [key.ascii, key.scan, 0, key.flags, sequence,
          0xa5 ^ key.ascii ^ key.scan ^ key.flags ^ sequence], 'C64 keyboard differs from MPE5 input envelope');
        this.keys.push(event);
        cpu.ram[K.ack] = sequence;
        this.releaseKey = true;
      } else assert.fail(`Unexpected DOS terminal command ${value}`);
    }
    if (address !== C.ack || !this.started) return;
    const packet = packets[this.ordinal];
    assert.equal(value, packet[4], 'Terminal ACK differs from pending firmware sequence');
    if (scrolling) {
      assert.equal(cpu.ram[0xd011] & 0x10, 0x10, 'Boulder scroll packet left the C64 display hidden');
      assert.equal(cpu.ram[T.transitionHidden], 0, 'Boulder scroll incorrectly began a hidden mode transition');
      this.visibleScrollPackets++;
    }
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
        if (packet[offset + 10] === 0x10 && (packet[offset + 11] & 15) === 1)
          repairPackedPathSeparator(this.expected, cell * 8);
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
      if (expectedFrame?.sharp && this.ordinal >= expectedFrame.sharp.firstPacket &&
          this.ordinal < expectedFrame.sharp.firstPacket + expectedFrame.sharp.packetCount) {
        if (hires) this.sharpHiresFrames++; else this.sharpColorFrames++;
      }
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
const cpu = new C64TerminalCpu(program, service, {rasterInterruptPeriod: 6000, recordWrites: false});
cpu.runUntil(machine => service.acks === packets.length || machine.pc === program.labels.terminal_error_hold,
  Math.max(5_000_000, packets.length * 50_000));
assert.notEqual(cpu.pc, program.labels.terminal_error_hold,
  `C64 terminal error ${cpu.ram[T.error]} after ${service.acks} packets`);
assert.equal(cpu.ram[T.error], 0);
assert.equal(service.acks, packets.length);
assert.equal(service.keys.length, wantedKeys.length, 'Terminal did not emit the expected keyboard events');
if (scroll) assert.equal(service.visibleScrollPackets, scroll.packetCount);
if (expectedFrame?.sharp) {
  assert.ok(service.sharpHiresFrames > 0, 'Sharp shortcut never displayed a complete hires frame');
  assert.ok(service.sharpColorFrames > 0, 'Sharp shortcut did not restore multicolor output');
}
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
let expectedPlanesHash = null;
if (options['expected-planes']) {
  const expectedPlanes = fs.readFileSync(options['expected-planes']);
  assert.equal(expectedPlanes.length, 10000, 'Firmware capture must contain bitmap, screen and colour planes');
  assert.deepEqual(finalPlanes, expectedPlanes, 'Final C64 planes differ from the independent firmware capture');
  expectedPlanesHash = sha256(expectedPlanes);
}
if (options.frame) {
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
// pixels. Each 8x8 C64 cell carries two supplied 4x8 DOS glyphs; compare
// every packed pair to the executed C64 bitmap.
function verifyConsole() {
const font = fs.readFileSync(options.font);
assert.equal(font.length, 256 * 8, 'Run the publication regression to export the verified 4x8 font');
const fontGlyph = character => font.subarray(character.charCodeAt(0) * 8, character.charCodeAt(0) * 8 + 8);
// V1.0.17 maps DOS ASCII backslash to the C64 pound-sign screen-code slot.
// The DOS-only receiver corrects that packed half-glyph so paths and prompts
// remain legible without requiring users to reflash otherwise working firmware.
const displayGlyph = character => character === '\\' ? pathSeparator : fontGlyph(character);
const sourceText = fs.readFileSync(options.text, 'utf8');
const lines = sourceText.replace(/\r\n?/g, '\n').split('\n');
if (lines.at(-1) === '') lines.pop();
assert.equal(lines.length, 25, 'Native console capture must have 25 rows');
let commaCells = 0, lowercaseCells = 0;
const distinctCharacters = new Set();
for (let row = 0; row < 25; row++) {
  assert.equal(lines[row].length, 80, `Console row ${row + 1} must have 80 characters`);
  for (let column = 0; column < 80; column += 2) {
    const left = lines[row][column], right = lines[row][column + 1];
    for (const character of [left, right]) {
      assert.ok(character >= ' ' && character <= '~', 'Console text must be printable ASCII');
      if (character !== '?') assert.notDeepEqual(fontGlyph(character), fontGlyph('?'),
        `Console character ${JSON.stringify(character)} falls back to '?'`);
      if (character === ',') commaCells++;
      if (character >= 'a' && character <= 'z') lowercaseCells++;
      distinctCharacters.add(character);
    }
    const cell = row * 40 + column / 2;
    const expected = Buffer.alloc(8);
    const leftGlyph = displayGlyph(left), rightGlyph = displayGlyph(right);
    for (let pixelRow = 0; pixelRow < 8; pixelRow++)
      expected[pixelRow] = leftGlyph[pixelRow] | (rightGlyph[pixelRow] >>> 4);
    const actual = finalPlanes.subarray(cell * 8, cell * 8 + 8);
    assert.deepEqual(actual.subarray(0, 7), expected.subarray(0, 7),
      `C64 row ${row + 1}, columns ${column + 1}-${column + 2} differ from the packed 4x8 glyphs`);
    // The final row can carry the firmware's blinking cursor underline.
    assert.ok([expected[7], expected[7] | 0xf0, expected[7] | 0x0f, expected[7] | 0xff].includes(actual[7]),
      `C64 row ${row + 1}, columns ${column + 1}-${column + 2} have an invalid cursor underline`);
    if (left !== ' ' || right !== ' ')
      assert.notEqual(finalPlanes[8000 + cell] >>> 4, finalPlanes[8000 + cell] & 15,
        `C64 row ${row + 1}, columns ${column + 1}-${column + 2} have invisible foreground`);
  }
}
assert.ok(commaCells > 0, 'DIR capture must include visible comma punctuation');
assert.ok(lowercaseCells > 0, 'Console capture must exercise lowercase text');
assert.ok(lines.some(line => line.includes('BOULDER  EXE')),
  'Final C64 bitmap does not show the BOULDER.EXE directory entry');
assert.ok(lines.some(line => line.startsWith('C:\\>')),
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
  visibleScrollPackets: service.visibleScrollPackets,
  sidRegisterFramesVerified: service.frames,
  distinctSidStates: service.sidStates.size,
  audibleSidFrames: service.audibleFrames,
  speakerGateOnTransitions: service.gateOnTransitions,
  speakerGateOffTransitions: service.gateOffTransitions,
  initialUniqueCells: service.initialSeen.size,
  inputEvents: service.keys,
  inputProof,
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
  `MPE5 C64 wire replay passed: ${service.acks} firmware packets, ${service.frames} hires frames, all 2,000 console characters in 1,000 packed cells including ${consoleProof.visibleCommaCells} commas, returned C:\\> prompt, and DIR/Return keyboard events.`);
