'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');

// Execute the current assembled desktop. Only the external IO, input and drawing
// boundaries are mocked; the file-operation dispatch and modal logic run as 6502.
const menuDir = path.resolve(__dirname, '..');
const probe = fs.readFileSync(path.join(__dirname, 'geos-color-publication.test.js'), 'utf8');
const start = probe.indexOf('class Cpu6502 {');
const end = probe.indexOf("test('assembled renderer", start);
assert.ok(start >= 0 && end > start, 'shared machine-code probe exists');
const Cpu6502 = vm.runInNewContext(probe.slice(start, end) + '\nCpu6502;', { assert });
const preview = fs.readFileSync(path.join(menuDir, 'preview-desktop.ps1'), 'utf8');
const acme = process.env.ACME_EXE || preview.match(/\$AcmePath\s*=\s*'([^']+)'/)[1];

test('file operations execute current desktop command routing and confirmation', async t => {
    if (!fs.existsSync(acme)) return t.skip('ACME unavailable; set ACME_EXE');
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'teensyrom-fileops-'));
    try {
        const binary = path.join(temporary, 'desktop.bin');
        const symbolFile = path.join(temporary, 'symbols');
        const result = spawnSync(acme, ['--format', 'plain', '--symbollist', symbolFile,
            '--outfile', binary, 'source/DesktopShellCode.asm'],
        { cwd: menuDir, encoding: 'utf8', timeout: 30000, windowsHide: true });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stdout + result.stderr);
        const s = Object.fromEntries([...fs.readFileSync(symbolFile, 'utf8')
            .matchAll(/^\s*(\w+)\s*=\s*\$([0-9a-f]+)/gmi)]
            .map(match => [match[1], parseInt(match[2], 16)]));
        const desktop = fs.readFileSync(binary);
        assert.ok(desktop.length <= 0x5800, `desktop uses ${desktop.length}/22528 bytes`);
        const stub = (cpu, name, callback = () => {}) => {
            assert.ok(Number.isInteger(s[name]), `stub symbol ${name} exists`);
            cpu.m[s[name]] = 0x60;
            cpu.hooks.set(s[name], callback);
        };

        const fresh = (options = {}) => {
            const memory = Buffer.alloc(65536);
            desktop.copy(memory, s.MainCodeRAMStart);
            memory[1] = 0x36;
            memory[s.GeosViewMode] = 1;
            memory[s.GeosSurfaceMode] = options.surface ?? s.GeosSurfaceBrowser;
            memory[s.IO1Port + s.rWRegCurrMenuWAIT] = options.source ?? s.rmtSD;
            memory[s.IO1Port + s.rwRegCursorItemOnPg] = 7;
            memory[s.Joystick2Sample] = 255;
            memory[s.GeosFileLastJoy] = 255;
            const cpu = new Cpu6502(memory);
            const state = {
                status: options.status ?? 0, progress: 0, commands: [], writes: [],
                events: [...(options.events || [])], redraws: 0, waits: 0,
                rows: [], characters: [], strings: [], percentages: [],
                selector: 0, serialIndex: 0, serialReads: [],
                name: Buffer.from(options.name || 'EXAMPLE.PRG', 'ascii'),
                message: Buffer.from(options.message || 'RESULT', 'ascii'),
            };
            const io = s.IO1Port;
            const originalStep = cpu.step.bind(cpu);
            cpu.step = () => {
                const opcode = cpu.m[cpu.pc];
                const address = cpu.m[(cpu.pc + 1) & 65535] | cpu.m[(cpu.pc + 2) & 65535] << 8;
                if (opcode === 0xad) {
                    if (address === io + s.rRegFileOpState) cpu.m[address] = state.status;
                    if (address === io + s.rRegFileOpProgress) cpu.m[address] = state.progress;
                    if (address === io + s.rwRegSerialString) {
                        const bytes = state.selector === s.rsstFileOpName ? state.name : state.message;
                        cpu.m[address] = bytes[state.serialIndex++] || 0;
                        state.serialReads.push([state.selector, cpu.m[address]]);
                    }
                }
                originalStep();
            };
            cpu.onWrite = (address, value) => {
                if (address < io || address >= io + 256) return;
                state.writes.push([address - io, value]);
                if (address === io + s.rwRegSerialString) {
                    state.selector = value;
                    state.serialIndex = 0;
                }
                if (address !== io + s.wRegControl) return;
                state.commands.push(value);
                const transitions = options.transitions || { 57: 2, 58: 3, 59: 6, 60: 5, 61: 3 };
                if (Object.hasOwn(transitions, value)) state.status = transitions[value];
            };
            stub(cpu, 'WaitForTRWaitMsg', () => { state.waits++; });
            stub(cpu, 'GeosShellRedraw', () => { state.redraws++; });
            stub(cpu, 'Mouse1351HideForRedraw');
            stub(cpu, 'Mouse1351ShowPointer');
            stub(cpu, 'GeosBitmapBlankLine', current => { state.rows.push(current.x); });
            stub(cpu, 'GeosBitmapSetCursor', current => {
                state.row = current.x;
                state.column = current.y;
                current.m[s.GeosBitmapRow] = current.x;
                current.m[s.GeosBitmapCol] = current.y;
            });
            stub(cpu, 'GeosBitmapPutChar', current => {
                state.characters.push([state.row, state.column++, current.a]);
            });
            if (options.realNameGlyphs) {
                cpu.hooks.set(s.GeosFilePutNameChar, current => {
                    state.characters.push([current.m[s.GeosBitmapRow], current.m[s.GeosBitmapCol], current.a]);
                });
            } else {
                stub(cpu, 'GeosFilePutNameChar', current => {
                    state.characters.push([state.row, state.column++, current.a]);
                });
            }
            stub(cpu, 'GeosBitmapPrintString', current => {
                let address = current.a | current.y << 8;
                let text = '';
                while (current.m[address]) text += String.fromCharCode(current.m[address++] & 127);
                state.strings.push([state.row, state.column, text, current.m[s.GeosBitmapReverse]]);
            });
            stub(cpu, 'GeosBitmapPrintIntByte', current => { state.percentages.push(current.a); });
            stub(cpu, 'GetIn', current => {
                assert.ok(state.events.length, `modal exhausted input events: state ${state.status}, frontend ${current.m[s.GeosFileLastState]}, commands ${state.commands.join(',')}`);
                const event = state.events.shift();
                const value = typeof event === 'function' ? event(current, state) : event;
                current.a = current.nz(typeof value === 'string' ? value.charCodeAt(0) : value || 0);
            });
            return { cpu, state };
        };
        const key = (cpu, value) => {
            cpu.a = typeof value === 'string' ? value.charCodeAt(0) : value;
            cpu.call(s.GeosFileHandleKey);
            return !!(cpu.p & 1);
        };
        const run = (entry, options) => {
            const context = fresh(options);
            context.cpu.call(s[entry]);
            return context;
        };

        await t.test('protocol command, status and string values agree with the backend contract', () => {
            for (const [name, expected] of Object.entries({
                rCtlFileCopyWAIT: 57, rCtlFilePasteWAIT: 58, rCtlFileDeletePrepareWAIT: 59,
                rCtlFileCancel: 60, rCtlFileDeleteConfirmWAIT: 61,
                rRegFileOpState: 63, rsstFileOpName: 9, rsstFileOpMessage: 10,
            })) assert.equal(s[name], expected, name);
            assert.equal(s.GeosHomeIconCount, 8, 'no persistent Trash icon');
        });

        await t.test('home, IEC, Teensy flash and remote browsers issue no operation or selection writes', () => {
            for (const entry of ['GeosFileCopy', 'GeosFilePaste', 'GeosFileDelete']) {
                for (const [surface, source] of [[0, 1], [2, 1], [1, 2], [1, 3], [1, 255]]) {
                    const { cpu, state } = run(entry, { surface, source });
                    assert.deepEqual(state.writes, [], `${entry}: surface ${surface}, source ${source}`);
                    assert.equal(cpu.m[s.GeosNotice], s.GeosNoticeFileScope);
                    assert.equal(state.redraws, 1, 'unsupported scope shows an in-desktop notice');
                }
            }
        });

        await t.test('Copy and Paste route from the actual Edit menu for both removable sources', () => {
            for (const source of [s.rmtSD, s.rmtUSBDrive]) for (const [selection, command] of [[0, 57], [1, 58]]) {
                const { cpu, state } = fresh({ source, events: [s.ChrReturn] });
                cpu.m[s.GeosActiveMenu] = s.GeosMenuEdit;
                cpu.m[s.GeosMenuSelection] = selection;
                cpu.m[s.GeosOverlayMode] = s.GeosOverlayMenu;
                cpu.call(s.GeosShellMenuActivate);
                assert.deepEqual(state.commands, [command]);
                assert.equal(state.writes[0][0], s.rwRegSelItemOnPage);
                assert.equal(state.writes[0][1], 7, 'operation uses the highlighted backend selection');
                assert.equal(cpu.m[s.GeosOverlayMode], 0);
                assert.equal(state.redraws, 1);
                assert.ok(state.strings.some(([, , text]) => text.includes('OK')));
            }
        });

        await t.test('Shift+C/P/D dispatch operations while ordinary letters and open overlays keep their existing routing', () => {
            for (const [keycode, command] of [[0xc3, 57], [0xd0, 58], [0xc4, 59]]) {
                const { cpu, state } = fresh({ events: command === 59 ? [s.ChrReturn, s.ChrReturn] : [s.ChrReturn] });
                cpu.a = keycode;
                cpu.call(s.GeosShellHandleKey);
                assert.deepEqual(state.commands, command === 59 ? [59, 60] : [command]);
                assert.ok(cpu.p & 1, 'shortcut is consumed');
            }
            for (const keycode of [0x43, 0x50, 0x44]) {
                const { cpu, state } = fresh();
                cpu.a = keycode;
                cpu.call(s.GeosShellHandleKey);
                assert.deepEqual(state.commands, []);
                assert.equal(cpu.p & 1, 0, 'ordinary letters remain available to directory search');
            }
            for (const overlay of [s.GeosOverlayMenu, s.GeosOverlayControl, s.GeosOverlayArrange]) {
                const { cpu, state } = fresh();
                cpu.m[s.GeosOverlayMode] = overlay;
                cpu.a = 0xc4;
                cpu.call(s.GeosShellHandleKey);
                assert.deepEqual(state.commands, []);
            }
        });

        await t.test('Delete menu captures the target and RETURN defaults to CANCEL', () => {
            const { cpu, state } = fresh({ events: [s.ChrReturn, s.ChrReturn] });
            cpu.m[s.GeosActiveMenu] = s.GeosMenuFile;
            cpu.m[s.GeosMenuSelection] = 4;
            cpu.m[s.GeosFileChoice] = 1;
            cpu.m[s.MouseOpenArmed] = 1;
            cpu.call(s.GeosShellMenuActivate);
            assert.deepEqual(state.commands, [59, 60]);
            const cancel = state.strings.find(([, , text]) => text.includes('CANCEL'));
            const remove = state.strings.find(([, , text]) => text.includes('DELETE'));
            assert.equal(cancel[3], 1, 'CANCEL initially highlighted');
            assert.equal(remove[3], 0, 'DELETE initially unselected');
            assert.equal(cpu.m[s.MouseOpenArmed], 0);
            assert.equal(cpu.m[s.MouseClickEdge], 0);
        });

        await t.test('Y or explicitly selected DELETE sends confirmation only after preparation', () => {
            for (const events of [[0x59, s.ChrReturn], [0xd9, s.ChrReturn],
                [s.ChrCRSRRight, s.ChrReturn, s.ChrReturn]]) {
                const { state } = run('GeosFileDelete', { events });
                assert.deepEqual(state.commands, [59, 61]);
                assert.equal(state.waits, 2, 'prepare and confirm each wait for backend result');
            }
            for (const status of [0, 1, 2, 3, 4, 5]) {
                const { cpu, state } = fresh({ status });
                cpu.m[s.GeosFileLastState] = 6;
                cpu.m[s.GeosFileChoice] = 1;
                assert.equal(key(cpu, 'Y'), false);
                assert.equal(key(cpu, s.ChrReturn), false);
                assert.deepEqual(state.commands, [], `stale prompt cannot confirm current state ${status}`);
            }
        });

        await t.test('STOP, HOME, ESC and N cancel pending work and cannot navigate or launch', () => {
            for (const cancellation of [s.ChrStop, s.ChrHome, 27, 0x4e, 0xce]) {
                const { state } = run('GeosFileDelete', { events: [cancellation, s.ChrReturn] });
                assert.deepEqual(state.commands, [59, 60]);
            }
            const { state } = run('GeosFilePaste', {
                transitions: { 58: 1, 60: 5 },
                events: [s.ChrCRSRRight, s.ChrUpArrow, s.ChrF1, s.ChrHome, s.ChrReturn],
            });
            assert.deepEqual(state.commands, [58, 60]);
        });

        await t.test('joystick requires a fresh fire edge and shares default cancel and explicit delete', () => {
            const joy = value => cpu => { cpu.m[s.Joystick2Sample] = value; return 0; };
            const defaultCancel = run('GeosFileDelete', {
                events: [joy(0xef), s.ChrReturn],
            });
            assert.deepEqual(defaultCancel.state.commands, [59, 60]);
            const selectedDelete = run('GeosFileDelete', {
                events: [joy(0xf7), joy(0xff), joy(0xef), s.ChrReturn],
            });
            assert.deepEqual(selectedDelete.state.commands, [59, 61]);
            const { cpu } = fresh();
            cpu.m[s.Joystick2Sample] = cpu.m[s.GeosFileLastJoy] = 0xef;
            cpu.call(s.GeosFileJoystick);
            assert.equal(cpu.a, 0, 'held fire does not produce another activation');
            cpu.m[s.Joystick2Sample] = 0xff;
            cpu.call(s.GeosFileJoystick);
            assert.equal(cpu.a, 0, 'release is not an activation');
            cpu.m[s.Joystick2Sample] = 0xef;
            cpu.call(s.GeosFileJoystick);
            assert.equal(cpu.a, s.ChrReturn);
            const held = fresh({ events: [current => {
                assert.equal(current.m[s.MouseClickEdge], 0, 'opening click discarded before confirmation');
                assert.equal(current.m[s.GeosFileLastJoy], 0xef, 'opening fire is latched');
                return 0;
            }, s.ChrReturn, s.ChrReturn] });
            held.cpu.m[s.Joystick2Sample] = 0xef;
            held.cpu.m[s.MouseActive] = 1;
            held.cpu.m[s.MouseClickEdge] = 1;
            held.cpu.m[s.MouseLogicalX] = 100;
            held.cpu.m[s.MouseLogicalY] = 150;
            held.cpu.call(s.GeosFileDelete);
            assert.deepEqual(held.state.commands, [59, 60], 'held opening inputs cannot confirm DELETE');
        });

        await t.test('mouse accepts only visible buttons and shares the same confirmation handler', () => {
            const mouse = (x, y) => cpu => {
                cpu.m[s.MouseActive] = 1;
                cpu.m[s.MouseLogicalX] = x;
                cpu.m[s.MouseLogicalY] = y;
                cpu.m[s.MouseClickEdge] = 1;
                return 0;
            };
            for (const x of [20, 59]) for (const y of [144, 159]) {
                const { state } = run('GeosFileDelete', { events: [mouse(x, y), s.ChrReturn] });
                assert.deepEqual(state.commands, [59, 60], `cancel boundary ${x},${y}`);
            }
            for (const x of [96, 135]) for (const y of [144, 159]) {
                const { state } = run('GeosFileDelete', { events: [mouse(x, y), s.ChrReturn] });
                assert.deepEqual(state.commands, [59, 61], `delete boundary ${x},${y}`);
            }
            for (const [x, y] of [[19, 144], [60, 144], [95, 144], [136, 144], [96, 143], [96, 160]]) {
                const { cpu, state } = fresh({ status: 6 });
                cpu.m[s.GeosFileLastState] = 6;
                mouse(x, y)(cpu);
                cpu.call(s.GeosFileMouse);
                assert.equal(cpu.a, 0, `outside buttons ${x},${y}`);
                assert.deepEqual(state.commands, []);
                assert.equal(cpu.m[s.MouseClickEdge], 0, 'every sampled click is consumed');
            }
            const { cpu } = fresh({ status: 3 });
            cpu.m[s.GeosFileLastState] = 3;
            mouse(100, 150)(cpu);
            cpu.call(s.GeosFileMouse);
            assert.equal(cpu.a, 0, 'finished dialogs have no DELETE target');
        });

        await t.test('busy progress updates without redrawing the name and terminal results remain until acknowledged', () => {
            const progress = value => (_cpu, state) => { state.progress = value; return 0; };
            const { cpu, state } = run('GeosFilePaste', {
                transitions: { 58: 1 },
                events: [progress(25), progress(80), (_cpu, state) => { state.status = 3; return 0; },
                    'Y', s.ChrReturn],
            });
            assert.deepEqual(state.commands, [58]);
            assert.deepEqual(state.percentages, [0, 25, 80]);
            assert.equal(state.strings.filter(([, , text]) => text === 'FILE OPERATIONS').length, 2);
            assert.equal(state.redraws, 1, 'browser redraw occurs only after terminal acknowledgement');
            assert.equal(cpu.m[s.GeosFileLastState], 3);
            for (const status of [2, 3, 4, 5]) {
                const context = run('GeosFileCopy', { transitions: { 57: status }, events: ['Y', s.ChrReturn] });
                assert.deepEqual(context.state.commands, [57], `terminal state ${status} is read-only`);
            }
        });

        await t.test('the full 255-byte target is visible and control characters cannot escape its seven rows', () => {
            const name = String.fromCharCode(3, 13, 31, 127, 128, 164, 255) + 'A'.repeat(248);
            const { state } = run('GeosFileDelete', { name, events: [s.ChrReturn, s.ChrReturn] });
            const glyphs = state.characters.filter(([row]) => row >= 6 && row <= 12).slice(0, 255);
            assert.equal(glyphs.length, 255);
            assert.deepEqual(glyphs.slice(0, 7).map(([, , value]) => value), [63, 63, 63, 63, 63, 63, 63]);
            assert.ok(glyphs.every(([row, column]) => row >= 6 && row <= 12 && column >= 1 && column <= 38));
            assert.equal(glyphs[254][0], 12);
            assert.ok(state.serialReads.some(([selector, value]) => selector === s.rsstFileOpName && value === 0));
            assert.ok(state.writes.some(([register, selector]) => register === s.rwRegSerialString && selector === s.rsstFileOpMessage));
            assert.ok(state.rows.every(row => row >= 4 && row <= 20), 'dialog clears only its interior');
        });

        await t.test('filename punctuation draws the exact native glyphs despite the installed icon charset', () => {
            const name = 'GAME_FILE{1}.PRG';
            const { cpu, state } = fresh({ name, realNameGlyphs: true, events: [s.ChrReturn, s.ChrReturn] });
            // Corrupt the screen-code font deliberately: the name path must use
            // the native ASCII font, never PETSCII conversion or icon glyphs.
            cpu.m.fill(0x6d, s.GeosBitmapFontData, s.GeosBitmapFontData + 1024);
            cpu.m.fill(0xaa, s.GeosBitmapRAM, s.GeosBitmapRAMEnd);
            const expected = Buffer.from(cpu.m.subarray(s.GeosBitmapRAM, s.GeosBitmapRAMEnd));
            for (let index = 0; index < name.length; index++) {
                const glyph = s.GeosRichFont + (name.charCodeAt(index) - 32) * 8;
                const cell = 6 * 320 + (index + 1) * 8;
                cpu.m.copy(expected, cell, glyph, glyph + 8);
            }
            cpu.call(s.GeosFileDelete);
            assert.deepEqual(cpu.m.subarray(s.GeosBitmapRAM, s.GeosBitmapRAMEnd), expected,
                'only target name cells change, with exact eight-row native glyphs');
            const characters = state.characters.filter(([row]) => row === 6).slice(0, name.length);
            assert.equal(String.fromCharCode(...characters.map(([, , value]) => value)), name,
                'underscore and braces reach the glyph renderer as raw ASCII');
            const underscore = cpu.m.subarray(s.GeosRichFont + (95 - 32) * 8, s.GeosRichFont + (96 - 32) * 8);
            const dollar = cpu.m.subarray(s.GeosRichFont + (36 - 32) * 8, s.GeosRichFont + (37 - 32) * 8);
            assert.notDeepEqual(underscore, dollar, 'underscore cannot silently become a dollar sign');
            const glyph = code => cpu.m.subarray(s.GeosRichFont + (code - 32) * 8, s.GeosRichFont + (code - 31) * 8);
            for (let code = 32; code < 127; code++) {
                if (code !== 63) assert.notDeepEqual(glyph(code), glyph(63),
                    `printable ASCII ${code} has its own native glyph`);
                if (code >= 97 && code <= 122) assert.deepEqual(glyph(code), glyph(code - 32),
                    'lowercase deliberately shares the desktop uppercase letter style');
            }
        });
    } finally {
        fs.rmSync(temporary, { recursive: true, force: true });
    }
});
