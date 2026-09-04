// Execute the generated 6510 receiver with faults on individual IO2 reads.
// The model never mutates the producer's pending packet to make a retry pass.
import assert from 'node:assert/strict';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import test from 'node:test';
import {loadDosTerminal} from '../tools/dos_terminal.mjs';

const agi = path.resolve(import.meta.dirname, '../../../AGI-64');
const {C64TerminalCpu} = await import(pathToFileURL(path.join(agi, 'test/helpers/c64-terminal-cpu.mjs')));
const {crc16Ccitt} = await import(pathToFileURL(path.join(agi, 'host/save-disk.mjs')));
const {buildMpe3TitleTerminal, MPE3_TITLE_TERMINAL_STATE: state} = await loadDosTerminal(agi);
const program = buildMpe3TitleTerminal({gameplay: true, enable1351Mouse: false});
const command = 0xdff4, status = 0xdff5, ack = 0xdff6, commit = 0xdff7;
function packet(type, sequence, flags, payload) {
  const bytes = Buffer.alloc(240);
  bytes.set([0x4d, 0x33, 1, type, sequence, flags, payload.length, 0]);
  bytes.set(payload, 8);
  bytes.writeUInt16LE(crc16Ccitt(bytes.subarray(0, 8 + payload.length)), 8 + payload.length);
  return bytes;
}
const cell = (index, seed) => Buffer.from([index, 0,
  ...Array.from({length: 8}, (_, n) => (seed + n * 17) & 255), 0x10, 1]);
const records = [cell(7, 0x31), cell(8, 0x72)];
const packets = [packet(1, 0x33, 0x0b, records[0]),
  packet(2, 0x34, 0x21, Buffer.alloc(27)),
  packet(1, 0x35, 0x08, records[1]),
  packet(2, 0x36, 0x21, Buffer.alloc(27))];

function run({fault = 'none', permanent = false, responds = true,
  droppedRequests = 0, firmwareError = false, lateFaultTicks = 0,
  faultIndex = 1, transientFaultReads = 0} = {}) {
  let started = false, index = 0, held = false, requested = false;
  let polls = 0, requests = 0, faults = 0, commitReads = 0, quietTick = 0;
  const acks = [], snapshots = [];
  const publish = cpu => {
    cpu.ram.set(packets[index], 0xdf00);
    cpu.ram[commit] = packets[index][4];
    cpu.ram[status] = 2;
  };
  const service = {
    onWrite(cpu, address, value) {
      if (address === command && value === 1 && !started) {
        started = true; publish(cpu);
      } else if (address === command && value === 4 && started) {
        requests++;
        // The CPU performs the store, but a lost bus write does not latch
        // the firmware request. A later identical command can still arrive.
        if (requests > droppedRequests) requested = true;
        snapshots.push(Buffer.from(cpu.ram.subarray(0xdf00, 0xdff0)));
      } else if (address === ack && started) {
        assert.equal(value, packets[index][4], 'receiver ACKed a corrupted/stale sequence');
        assert.deepEqual(Buffer.from(cpu.ram.subarray(0xdf00, 0xdff0)), packets[index],
          'pending bytes changed during recovery');
        acks.push(value); held = requested = false; polls = 0;
        if (++index < packets.length) publish(cpu);
      }
    },
    onRead(cpu, address) {
      // Model the remaining foreground slice: the ISR request is not the
      // ready signal. The receiver must wait for a later foreground quiesce.
      if (requested && responds && address === status && ++polls >= 128) {
        if (firmwareError) {
          cpu.ram[status] = 0xe0;
          cpu.ram[0xdffb] = 0x41;
        } else {
          held = true; quietTick = cpu.ram[state.rasterTicks]; cpu.ram[status] = 0x12;
        }
      }
    }
  };
  const cpu = new C64TerminalCpu(program, service, {rasterInterruptPeriod: 2000});
  const read = cpu.read.bind(cpu);
  cpu.read = address => {
    const value = read(address);
    const quietAge = (cpu.ram[state.rasterTicks] - quietTick) & 255;
    if (!started || index !== faultIndex ||
        (transientFaultReads && faults >= transientFaultReads) ||
        (!permanent && held && quietAge >= lateFaultTicks)) return value;
    let mask = 0;
    if (fault === 'commit' && address === commit) mask = (++commitReads & 1) ? 8 : 0;
    if (fault === 'crc' && address === 0xdf0c) mask = 8;
    if (fault === 'length' && address === 0xdf06) mask = 0xff ^ value;
    if (mask) { faults++; return value ^ mask; }
    return value;
  };
  cpu.runUntil(c => acks.length === packets.length || c.pc === program.labels.terminal_error_hold, 3_000_000);
  return {cpu, acks, requests, polls, faults, snapshots};
}

test('normal DOS packets incur no quiet requests', () => {
  const result = run();
  assert.deepEqual(result.acks, [0x33, 0x34, 0x35, 0x36]);
  assert.equal(result.requests, 0);
});
test('bootstrap retries a transient first-packet read without waiting for an unarmed raster IRQ', () => {
  const result = run({fault: 'crc', faultIndex: 0, transientFaultReads: 1});
  assert.equal(result.faults, 1);
  assert.equal(result.requests, 0, 'pre-display recovery tried to wait on the inactive frame counter');
  assert.deepEqual(result.acks, [0x33, 0x34, 0x35, 0x36]);
  assert.equal(result.cpu.ram[state.error], 0);
});
test('paced retry waits out two frames of post-quiet IO2 residue', () => {
  const result = run({fault: 'crc', lateFaultTicks: 2});
  assert.ok(result.faults > 0);
  assert.ok(result.requests > 0, 'receiver did not request a quiet retry');
  assert.deepEqual(result.acks, [0x33, 0x34, 0x35, 0x36]);
  assert.equal(result.cpu.ram[state.error], 0);
});
for (const fault of ['commit', 'crc', 'length']) {
  test(`${fault} read fault recovers only after foreground quiet acknowledgement`, () => {
    const result = run({fault});
    assert.ok(result.faults > 0);
    assert.ok(result.requests > 0, 'receiver did not request a quiet retry');
    assert.deepEqual(result.acks, [0x33, 0x34, 0x35, 0x36]);
    assert.equal(result.cpu.ram[state.error], 0);
    for (const snapshot of result.snapshots) assert.deepEqual(snapshot, packets[1]);
    for (const record of records) assert.deepEqual(
      Buffer.from(result.cpu.ram.subarray(0x6000 + record[0] * 8, 0x6008 + record[0] * 8)),
      record.subarray(2, 10));
  });
}
for (const [fault, error] of [['commit', 12], ['crc', 6], ['length', 4]]) {
  test(`persistent ${fault} corruption stays bounded and never ACKs a bad packet`, () => {
    const result = run({fault, permanent: true});
    assert.ok(result.requests > 0);
    assert.deepEqual(result.acks, [0x33]);
    assert.equal(result.cpu.ram[state.error], error);
    assert.equal(result.cpu.pc, program.labels.terminal_error_hold);
  });
}
test('missing quiet response is bounded without consuming the damaged packet', () => {
  const result = run({fault: 'commit', responds: false});
  assert.ok(result.requests > 0);
  assert.deepEqual(result.acks, [0x33]);
  assert.equal(result.cpu.ram[state.error], 12);
});

test('a dropped first quiet command is resent without acknowledging damaged data', () => {
  const result = run({fault: 'crc', droppedRequests: 1});
  assert.equal(result.requests, 2);
  assert.deepEqual(result.acks, [0x33, 0x34, 0x35, 0x36]);
  assert.equal(result.cpu.ram[state.error], 0);
  assert.equal(result.snapshots.length, 2);
  for (const snapshot of result.snapshots) assert.deepEqual(snapshot, packets[1]);
});

test('firmware error during quiet wait remains terminal without an ACK', () => {
  const result = run({fault: 'commit', firmwareError: true});
  assert.equal(result.requests, 1);
  assert.deepEqual(result.acks, [0x33]);
  assert.equal(result.cpu.ram[state.error], 3);
  assert.equal(result.cpu.ram[status], 0xe0);
  assert.equal(result.cpu.ram[0xdffb], 0x41);
  assert.equal(result.cpu.pc, program.labels.terminal_error_hold);
  assert.deepEqual(Buffer.from(result.cpu.ram.subarray(0xdf00, 0xdff0)), packets[1]);
});
