'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');

const menuDir = path.resolve(__dirname, '..');
const probe = fs.readFileSync(path.join(__dirname, 'geos-color-publication.test.js'), 'utf8');
const first = probe.indexOf('class Cpu6502 {');
const last = probe.indexOf("test('assembled renderer", first);
const Cpu6502 = vm.runInNewContext(probe.slice(first, last) + '\nCpu6502;', { assert });
const preview = fs.readFileSync(path.join(menuDir, 'preview-desktop.ps1'), 'utf8');
const acme = process.env.ACME_EXE || preview.match(/\$AcmePath\s*=\s*'([^']+)'/)[1];

test('assembled bitmap loading display and launch routing', async t => {
    if (!fs.existsSync(acme)) return t.skip('ACME unavailable; set ACME_EXE');
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'teensyrom-loading-'));
    try {
        const binary = path.join(temporary, 'desktop.bin');
        const labels = path.join(temporary, 'symbols');
        const assembled = spawnSync(acme, ['--format', 'plain', '--symbollist', labels,
            '--outfile', binary, 'source/DesktopShellCode.asm'],
        { cwd: menuDir, encoding: 'utf8', timeout: 30000, windowsHide: true });
        assert.ifError(assembled.error);
        assert.equal(assembled.status, 0, assembled.stdout + assembled.stderr);
        const s = Object.fromEntries([...fs.readFileSync(labels, 'utf8')
            .matchAll(/^\s*(\w+)\s*=\s*\$([0-9a-f]+)/gmi)]
            .map(match => [match[1], parseInt(match[2], 16)]));
        const desktop = fs.readFileSync(binary);
        assert.ok(desktop.length <= 22528, `desktop uses ${desktop.length}/22528 bytes`);
        t.diagnostic(`desktop uses ${desktop.length}/22528 bytes`);
        const stub = (cpu, label, callback = () => {}) => {
            assert.ok(Number.isInteger(s[label]), label);
            cpu.m[s[label]] = 0x60;
            cpu.hooks.set(s[label], callback);
        };
        const fresh = () => {
            const memory = Buffer.alloc(65536);
            desktop.copy(memory, s.MainCodeRAMStart);
            memory.fill(0x55, s.GeosBitmapRAM, s.GeosBitmapRAMEnd);
            memory.fill(0xaa, s.GeosRichCanvas, s.GeosRichCanvas + 8000);
            memory.fill(0x61, s.C64ScreenRAM, s.C64ScreenRAM + 1000);
            memory[1] = 0x37;
            memory[s.GeosBitmapActive] = 1;
            memory[s.TODTenthSecBCD] = 3;
            return new Cpu6502(memory);
        };
        const pixel = (cpu, x, y) => !!(cpu.m[s.GeosBitmapRAM + (y >> 3) * 320 + (x >> 3) * 8 + (y & 7)] & (128 >> (x & 7)));
        const region = (cpu, x, y, width, height) => Buffer.from(Array.from({ length: width * height },
            (_, index) => Number(pixel(cpu, x + index % width, y + Math.floor(index / width)))));
        const outsideIntact = cpu => {
            for (let row = 0; row < 25; row++) for (let column = 0; column < 40; column++) {
                if (row >= 7 && row < 21 && column >= 6 && column < 34) continue;
                const bitmap = s.GeosBitmapRAM + row * 320 + column * 8;
                assert.deepEqual(cpu.m.subarray(bitmap, bitmap + 8), Buffer.alloc(8, 0x55), 'outside bitmap retained');
                assert.equal(cpu.m[s.C64ScreenRAM + row * 40 + column], 0x61, 'outside palette retained');
            }
        };
        const localMessage = (cpu, text, label = 'GeosBitmapWaitLocalMessage') => {
            const address = 0x18f0; // Exercise a string crossing a 256-byte page.
            Buffer.from(text + '\0').copy(cpu.m, address);
            cpu.a = address & 255; cpu.y = address >> 8;
            cpu.call(s[label]);
        };

        await t.test('panel publishes only its centered rectangle, pixels before colors', () => {
            const cpu = fresh(), writes = [];
            cpu.p &= ~4;
            cpu.onWrite = address => { writes.push(address); };
            cpu.call(s.GeosBitmapWaitBegin);
            assert.equal(cpu.m[1], 0x37, 'BASIC bank restored');
            assert.equal(cpu.p & 4, 0, 'caller IRQ state restored');
            assert.equal(cpu.m[s.GeosBitmapActive], 1, 'bitmap remains active');
            for (let row = 0; row < 25; row++) for (let column = 0; column < 40; column++) {
                const inside = row >= 7 && row < 21 && column >= 6 && column < 34;
                const bitmap = s.GeosBitmapRAM + row * 320 + column * 8;
                const color = s.C64ScreenRAM + row * 40 + column;
                if (!inside) {
                    assert.deepEqual(cpu.m.subarray(bitmap, bitmap + 8), Buffer.alloc(8, 0x55));
                    assert.equal(cpu.m[color], 0x61, 'outside palette retained');
                } else assert.equal(cpu.m[color], 0x01, 'panel uses black on white');
            }
            for (let x = 48; x < 272; x++) assert.ok(pixel(cpu, x, 56) && pixel(cpu, x, 167));
            for (let y = 56; y < 168; y++) assert.ok(pixel(cpu, 48, y) && pixel(cpu, 271, y));
            assert.equal(pixel(cpu, 49, 57), false, 'inset is white');
            const pixelWrites = writes.map((address, index) => [address, index])
                .filter(([address]) => address >= s.GeosBitmapRAM && address < s.GeosBitmapRAMEnd);
            const firstColor = writes.findIndex(address => address >= s.C64ScreenRAM && address < s.C64ScreenRAM + 1000);
            assert.ok(pixelWrites.at(-1)[1] < firstColor, 'complete panel precedes palette publication');
            assert.ok(!writes.some(address => [0xd011, 0xd016, 0xd018].includes(address)), 'no VIC mode changes');
        });

        await t.test('activity segment uses CIA tenths, loops, and never claims a percentage', () => {
            const cpu = fresh();
            cpu.call(s.GeosBitmapWaitBegin);
            const initial = Buffer.from(cpu.m.subarray(s.GeosBitmapRAM, s.GeosBitmapRAMEnd));
            cpu.call(s.GeosBitmapWaitAnimate);
            assert.deepEqual(cpu.m.subarray(s.GeosBitmapRAM, s.GeosBitmapRAMEnd), initial, 'same tick does no drawing');
            for (let step = 1; step <= 21; step++) {
                cpu.m[s.TODTenthSecBCD] = (3 + step) % 10;
                cpu.call(s.GeosBitmapWaitAnimate);
                const phase = step % 21;
                assert.equal(cpu.m[s.GeosBitmapWaitPhase], phase);
                for (let x = 66; x < 254; x++) {
                    assert.equal(pixel(cpu, x, 152), x >= 66 + phase * 8 && x < 90 + phase * 8, 'one bounded activity segment');
                }
                assert.equal(cpu.p & 4, 4, 'animation also works while IRQs remain disabled');
                assert.equal(cpu.m[1], 0x37);
            }
            assert.deepEqual(cpu.m.subarray(s.GeosBitmapRAM, s.GeosBitmapRAMEnd), initial, 'segment repeats without a completion state');
            const title = cpu.m.subarray(s.MsgGeosLoading, s.MsgGeosLoadStopped);
            assert.equal(Buffer.from(title).map(value => value & 127).toString('ascii'), 'LOADING...\0');
        });

        await t.test('stable-ready handshake rejects a transient ready and fully drains messages', () => {
            const cpu = fresh(), ioStatus = s.IO1Port + s.rwRegStatus, ioString = s.IO1Port + s.rwRegSerialString;
            const statuses = [s.rsReady, 0, ...Array(6).fill(s.rsC64Message), ...Array(6).fill(s.rsReady)];
            const message = Buffer.concat([Buffer.from([5, 13, 0x90]), Buffer.from('FILE ERROR:'),
                Buffer.from([13, 0xc1]), Buffer.from(' ' + 'X'.repeat(180)), Buffer.from([0])]);
            const visible = [], positions = [], acknowledgements = [];
            let reads = 0, serial = 0, selected = false;
            const step = cpu.step.bind(cpu);
            cpu.step = () => {
                const opcode = cpu.m[cpu.pc], address = cpu.m[cpu.pc + 1] | cpu.m[cpu.pc + 2] << 8;
                if ([0xad, 0xcd].includes(opcode) && address === ioStatus) {
                    assert.ok(statuses.length, 'wait consumed only the expected stable status reads');
                    cpu.m[address] = statuses.shift(); reads++;
                }
                if (opcode === 0xad && address === ioString) {
                    assert.ok(selected, 'serial selector written before reading');
                    cpu.m[address] = message[serial++];
                }
                step();
            };
            cpu.onWrite = (address, value) => {
                if (address === ioString) {
                    assert.equal(value, s.rsstSerialStringBuf); selected = true;
                }
                if (address === ioStatus) acknowledgements.push([value, reads, serial]);
            };
            cpu.hooks.set(s.GeosBitmapWaitMessageGlyph, current => {
                visible.push(current.a);
                positions.push([current.m[s.RichX] + current.m[s.RichXHi] * 256, current.m[s.RichY]]);
            });
            cpu.call(s.WaitForTRWaitMsg);
            assert.deepEqual(acknowledgements, [[s.rsContinue, 8, message.length]], 'ack only after stable message and complete drain');
            assert.equal(statuses.length, 0);
            assert.equal(visible.length, 170, 'long message remains bounded to five rows');
            assert.equal(Buffer.from(visible.slice(0, 14)).toString('ascii'), 'FILE ERROR: A ');
            assert.ok(visible.every(value => value >= 32 && value < 128), 'control bytes are not glyphs');
            assert.deepEqual(positions, Array.from({ length: 170 }, (_, index) => [58 + index % 34 * 6, 84 + Math.floor(index / 34) * 10]));
            outsideIntact(cpu);
        });

        await t.test('failed launch keeps every message pixel inside the modal before acknowledgement', () => {
            const cpu = fresh();
            cpu.call(s.GeosBitmapWaitBegin);
            localMessage(cpu, 'FILE COULD NOT BE LOADED. ' + 'CHECK THE MEDIA AND TRY AGAIN. '.repeat(6));
            const message = region(cpu, 58, 82, 204, 52);
            assert.ok(message.includes(1), 'actual message glyphs were drawn');
            let inputCalls = 0;
            stub(cpu, 'IRQEnable');
            stub(cpu, 'CheckForIRQGetIn', current => {
                assert.deepEqual(region(current, 58, 82, 204, 52), message, 'message visible before key acknowledgement');
                inputCalls++; current.a = current.nz(1);
            });
            stub(cpu, 'PrintString', () => assert.fail('bitmap error must not use KERNAL text'));
            cpu.call(s.AnyKeyErrMsgWait);
            assert.equal(inputCalls, 1, 'existing any-key wait remains the return path');
            assert.equal(pixel(cpu, 64, 148), false, 'activity track removed after failure');
            assert.deepEqual(region(cpu, 58, 82, 204, 52), message);
            outsideIntact(cpu);
        });

        await t.test('a shorter update clears stale text while preserving heading and activity', () => {
            const cpu = fresh();
            cpu.call(s.GeosBitmapWaitBegin);
            const heading = region(cpu, 64, 64, 192, 10), track = region(cpu, 64, 147, 192, 10);
            localMessage(cpu, 'X'.repeat(220));
            assert.ok(region(cpu, 58, 124, 204, 7).includes(1), 'long text reaches fifth line');
            localMessage(cpu, 'READY');
            assert.ok(region(cpu, 58, 84, 30, 7).includes(1));
            assert.ok(region(cpu, 58, 94, 204, 40).every(value => value === 0), 'old lower lines cleared');
            assert.deepEqual(region(cpu, 64, 64, 192, 10), heading);
            assert.deepEqual(region(cpu, 64, 147, 192, 10), track);
            cpu.m[s.TODTenthSecBCD]++;
            cpu.call(s.GeosBitmapWaitAnimate);
            assert.ok(region(cpu, 58, 84, 30, 7).includes(1), 'animation preserves latest message');
            outsideIntact(cpu);
        });

        await t.test('local information panel draws without Teensy IO or an input wait', () => {
            const cpu = fresh(), headings = [];
            cpu.p &= ~4;
            cpu.hooks.set(s.RichText, current => { headings.push(current.a | current.y << 8); });
            const step = cpu.step.bind(cpu);
            cpu.step = () => {
                const opcode = cpu.m[cpu.pc], address = cpu.m[cpu.pc + 1] | cpu.m[cpu.pc + 2] << 8;
                assert.ok(!([0xad, 0x8d, 0xcd].includes(opcode) && address >= s.IO1Port && address < s.IO1Port + 256), 'draw-only helper avoids Teensy registers');
                step();
            };
            localMessage(cpu, 'DEVICE NOT PRESENT. CHECK THE DRIVE CONNECTION AND TRY AGAIN.', 'GeosBitmapShowMessage');
            assert.deepEqual(headings, [s.MsgGeosInformation, s.MsgGeosLoadContinue]);
            assert.ok(region(cpu, 58, 94, 204, 7).includes(1), 'local message wraps inside panel');
            assert.ok(region(cpu, 121, 149, 78, 7).includes(1), 'information shows its dismissal prompt');
            assert.equal(pixel(cpu, 64, 148), false, 'information has no activity track');
            assert.equal(cpu.m[1], 0x37);
            assert.equal(cpu.p & 4, 0);
            outsideIntact(cpu);
        });

        const launch = (type, bitmap, answer = 78) => {
            const cpu = fresh(), calls = [];
            cpu.m[s.GeosBitmapActive] = bitmap;
            cpu.m[s.IO1Port + s.rRegItemTypePlusIOH] = type;
            cpu.m[s.IO1Port + s.rRegStrAvailable] = 1;
            for (const label of ['Mouse1351Hide', 'IRQDisable', 'IRQEnable', 'SendChar', 'PrintSerialString', 'PrintString',
                'SIDLoadInit', 'ShowSIDInfoPage', 'LoadViewKoala', 'XferCopyRun', 'AnyKeyErrMsgWait', 'ListMenuItems']) {
                stub(cpu, label, () => { calls.push(label); });
            }
            stub(cpu, 'PrintBanner', current => { calls.push('PrintBanner'); current.m[s.GeosBitmapActive] = 0; });
            stub(cpu, 'GetIn', current => { current.a = current.nz(answer); });
            stub(cpu, 'StartSelItem_WaitForTRDots', current => { calls.push(['start', current.m[s.GeosBitmapActive]]); });
            cpu.call(s.RunSelected);
            return calls;
        };

        await t.test('ROM and PRG launches retain bitmap while classic launches keep their banner', () => {
            for (const type of [s.rtFileCrt, s.rtFilePrg, s.rtFileP00]) for (const bitmap of [0, 1]) {
                const calls = launch(type, bitmap);
                assert.deepEqual(calls.filter(Array.isArray), [['start', bitmap]]);
                assert.equal(calls.includes('PrintBanner'), !bitmap);
                assert.ok(calls.includes('XferCopyRun'));
            }
        });

        await t.test('firmware confirmation and SID/art viewers retain their legacy paths', () => {
            const cancelled = launch(s.rtFileHex, 1);
            assert.ok(cancelled.includes('PrintBanner'));
            assert.deepEqual(cancelled.filter(Array.isArray), [], 'N must never start a firmware update');
            const confirmed = launch(s.rtFileHex, 1, 89);
            assert.deepEqual(confirmed.filter(Array.isArray), [['start', 0]], 'Y retains the confirmed text-mode update');
            for (const [type, viewer] of [[s.rtFileSID, 'SIDLoadInit'], [s.rtFileKla, 'LoadViewKoala'], [s.rtFileArt, 'LoadViewKoala']]) {
                const calls = launch(type, 1);
                assert.ok(calls.includes('PrintBanner'));
                assert.ok(calls.includes(viewer));
            }
        });
    } finally {
        fs.rmSync(temporary, { recursive: true, force: true });
    }
});
