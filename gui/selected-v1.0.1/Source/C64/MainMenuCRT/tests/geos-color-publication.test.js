'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const menuDir = path.resolve(__dirname, '..');
const source = name => fs.readFileSync(path.join(menuDir, 'source', name), 'utf8').replace(/;[^\r\n]*/g, '');
const bitmap = source('GeosBitmap.s');
const rich = source('GeosRich.s');
function block(text, first, last) {
    const start = text.indexOf(first);
    const end = text.indexOf(last, start + first.length);
    assert.ok(start >= 0 && end > start, `${first} through ${last} exists`);
    return text.slice(start, end);
}

test('a full redraw consumes layout characters into pending colors, not the visible palette', () => {
    const start = block(bitmap, 'GeosBitmapConvertScreen:', 'GeosBitmapConvertCell:');
    assert.match(start, /jsr GeosRichBegin\s+lda #>\(GeosLayoutScreen-C64ScreenRAM\)\s+sta GeosBitmapColorOffset/);
    assert.match(start, /lda TblGeosBitmapScreenRowHi,x\s+clc\s+adc #>\(GeosLayoutScreen-C64ScreenRAM\)\s+sta smcGeosBitmapReadCell\+2\s+sta smcGeosBitmapWriteCell\+2/);
    assert.match(block(bitmap, 'GeosBitmapTintRow:', 'GeosBitmapSetCursor:'), /adc GeosBitmapColorOffset\s+sta smcGeosBitmapTintWrite\+2/);
    assert.match(block(bitmap, 'GeosBitmapSetItemLabelColor:', 'GeosBitmapDrawBrowserStatus:'), /adc GeosBitmapColorOffset\s+sta smcGeosBitmapLabelColor\+2/);
});

test('native home, header, and panels never recolor the previous visible frame', () => {
    assert.doesNotMatch(rich, /\bsta\s+(?:\$0[4-7][0-9a-f]{2}\b|C64ScreenRAM\b|GeosBitmapScreen\b)/i);
    assert.match(block(rich, 'GeosRichHome:', 'RichHomeIcon:'), /sta GeosLayoutScreen,x[\s\S]*sta GeosLayoutScreen\+\$2e8,x/);
    assert.match(block(rich, 'GeosRichBar:', 'GeosRichMenu:'), /sta GeosLayoutScreen,x/);
    assert.match(block(rich, 'RichPanelColorRow:', 'GeosRichControl:'), /adc #>\(GeosLayoutScreen-C64ScreenRAM\)\s+sta RichColorWrite\+2/);
});

test('all bitmap pixels publish before colors, and colors publish before bank restoration', () => {
    assert.match(block(rich, 'GeosRichCompose:', 'GeosRichPublish:'), /jsr GeosRichPublish\s+jsr GeosBitmapPublishColors\s+lda RichSavedBank\s+sta \$01\s+rts/);
    const publish = block(bitmap, 'GeosBitmapPublishColors:', 'GeosBitmapCaptureFont:');
    assert.match(publish, /!for page,0,2/);
    assert.match(publish, /lda GeosLayoutScreen\+768,x\s+cmp C64ScreenRAM\+768,x\s+beq \+\s+sta C64ScreenRAM\+768,x/);
    assert.match(publish, /cpx #232\s+bne GeosBitmapPublishColorTail\s+lda #0\s+sta GeosBitmapColorOffset\s+rts/);
    assert.doesNotMatch(publish, /\$d011|\$d016|VICMemSetup|Sprite0Pointer/);
});

// Assemble current production sources into a unique temporary directory. Never
// consume potentially stale build output or regenerate firmware/header files.
// Set ACME_EXE for other machines; the preview's default is a local fallback.
function findAssembler() {
    if (process.env.ACME_EXE) return process.env.ACME_EXE;
    const preview = fs.readFileSync(path.join(menuDir, 'preview-desktop.ps1'), 'utf8');
    const defaultPath = preview.match(/\$AcmePath\s*=\s*'([^']+)'/)[1];
    const candidates = [defaultPath, ...(process.env.PATH || '').split(path.delimiter)
        .map(directory => path.join(directory, process.platform === 'win32' ? 'acme.exe' : 'acme'))];
    return candidates.find(candidate => fs.existsSync(candidate));
}

// Instruction-level NMOS 6502 probe. It executes actual assembled renderer
// bytes, including self-modifying operands; no renderer algorithm is re-created.
// It deliberately does not model VIC raster timing, SID IRQs, or Teensy IO.
class Cpu6502 {
    constructor(memory) {
        this.m = memory;
        this.a = this.x = this.y = 0;
        this.p = 0x24;
        this.sp = 0xff;
        this.hooks = new Map();
        this.onWrite = () => {};
    }
    nz(value) {
        value &= 255;
        this.p = (this.p & ~0x82) | (value & 0x80) | (value === 0 ? 2 : 0);
        return value;
    }
    flag(mask, condition) { this.p = condition ? this.p | mask : this.p & ~mask; }
    byte() { const value = this.m[this.pc]; this.pc = (this.pc + 1) & 65535; return value; }
    word() { const low = this.byte(); return low | (this.byte() << 8); }
    write(address, value) {
        address &= 65535;
        value &= 255;
        this.onWrite(address, value, this);
        this.m[address] = value;
    }
    push(value) { this.write(0x100 + this.sp, value); this.sp = (this.sp - 1) & 255; }
    pop() { this.sp = (this.sp + 1) & 255; return this.m[0x100 + this.sp]; }
    address(mode) {
        switch (mode) {
        case 'imm': { const result = this.pc; this.pc = (this.pc + 1) & 65535; return result; }
        case 'zp': return this.byte();
        case 'zpx': return (this.byte() + this.x) & 255;
        case 'zpy': return (this.byte() + this.y) & 255;
        case 'abs': return this.word();
        case 'abx': return (this.word() + this.x) & 65535;
        case 'aby': return (this.word() + this.y) & 65535;
        case 'inx': { const p = (this.byte() + this.x) & 255; return this.m[p] | (this.m[(p + 1) & 255] << 8); }
        case 'iny': { const p = this.byte(); return ((this.m[p] | (this.m[(p + 1) & 255] << 8)) + this.y) & 65535; }
        default: throw new Error(`Unsupported addressing mode ${mode}`);
        }
    }
    call(address, limit = 3000000) {
        this.pc = address;
        this.push(0xff);
        this.push(0xfe); // RTS sentinel: $ffff.
        for (let steps = 0; this.pc !== 0xffff; steps++) {
            assert.ok(steps < limit, `6502 step limit at $${this.pc.toString(16)}`);
            this.hooks.get(this.pc)?.(this);
            this.step();
        }
        assert.equal(this.sp, 0xff, 'renderer balances the stack');
    }
    step() {
        const at = this.pc;
        const opcode = this.byte();
        const decoded = opcodes.get(opcode);
        assert.ok(decoded, `Unsupported opcode $${opcode.toString(16)} at $${at.toString(16)}`);
        const [op, mode] = decoded;
        if (op.startsWith('B') && op !== 'BIT') {
            const displacement = this.byte();
            const conditions = { BPL: !(this.p & 128), BMI: this.p & 128, BVC: !(this.p & 64), BVS: this.p & 64,
                BCC: !(this.p & 1), BCS: this.p & 1, BNE: !(this.p & 2), BEQ: this.p & 2 };
            if (conditions[op]) this.pc = (this.pc + (displacement < 128 ? displacement : displacement - 256)) & 65535;
            return;
        }
        const address = mode && mode !== 'acc' ? this.address(mode) : undefined;
        const value = mode === 'acc' ? this.a : this.m[address];
        const store = result => mode === 'acc' ? this.a = this.nz(result) : this.write(address, this.nz(result));
        switch (op) {
        case 'LDA': this.a = this.nz(value); break;
        case 'LDX': this.x = this.nz(value); break;
        case 'LDY': this.y = this.nz(value); break;
        case 'STA': this.write(address, this.a); break;
        case 'STX': this.write(address, this.x); break;
        case 'STY': this.write(address, this.y); break;
        case 'AND': this.a = this.nz(this.a & value); break;
        case 'ORA': this.a = this.nz(this.a | value); break;
        case 'EOR': this.a = this.nz(this.a ^ value); break;
        case 'ADC': case 'SBC': {
            assert.equal(this.p & 8, 0, 'these deterministic probe cases do not use decimal arithmetic');
            const operand = op === 'SBC' ? value ^ 255 : value;
            const sum = this.a + operand + (this.p & 1);
            this.flag(64, (~(this.a ^ operand) & (this.a ^ sum) & 128) !== 0);
            this.flag(1, sum > 255);
            this.a = this.nz(sum);
            break;
        }
        case 'CMP': case 'CPX': case 'CPY': {
            const register = op === 'CMP' ? this.a : op === 'CPX' ? this.x : this.y;
            this.flag(1, register >= value);
            this.nz(register - value);
            break;
        }
        case 'BIT': this.p = (this.p & ~0xc2) | (value & 0xc0) | ((this.a & value) === 0 ? 2 : 0); break;
        case 'ASL': this.flag(1, value & 128); store(value << 1); break;
        case 'LSR': this.flag(1, value & 1); store(value >> 1); break;
        case 'ROL': { const carry = this.p & 1; this.flag(1, value & 128); store((value << 1) | carry); break; }
        case 'ROR': { const carry = this.p & 1; this.flag(1, value & 1); store((value >> 1) | (carry << 7)); break; }
        case 'INC': store(value + 1); break;
        case 'DEC': store(value - 1); break;
        case 'INX': this.x = this.nz(this.x + 1); break;
        case 'DEX': this.x = this.nz(this.x - 1); break;
        case 'INY': this.y = this.nz(this.y + 1); break;
        case 'DEY': this.y = this.nz(this.y - 1); break;
        case 'TAX': this.x = this.nz(this.a); break;
        case 'TAY': this.y = this.nz(this.a); break;
        case 'TXA': this.a = this.nz(this.x); break;
        case 'TYA': this.a = this.nz(this.y); break;
        case 'PHA': this.push(this.a); break;
        case 'PLA': this.a = this.nz(this.pop()); break;
        case 'PHP': this.push(this.p | 0x30); break;
        case 'PLP': this.p = this.pop() | 0x20; break;
        case 'CLC': this.p &= ~1; break;
        case 'SEC': this.p |= 1; break;
        case 'SEI': this.p |= 4; break;
        case 'CLI': this.p &= ~4; break;
        case 'CLD': this.p &= ~8; break;
        case 'SED': this.p |= 8; break;
        case 'JMP': this.pc = address; break;
        case 'JSR': this.push((this.pc - 1) >> 8); this.push(this.pc - 1); this.pc = address; break;
        case 'RTS': { const low = this.pop(); this.pc = ((low | (this.pop() << 8)) + 1) & 65535; break; }
        case 'NOP': break;
        default: throw new Error(`Unsupported instruction ${op}`);
        }
    }
}

const opcodes = new Map();
for (const [operation, modes, codes] of [
    ['LDA', 'inx,zp,imm,abs,iny,zpx,aby,abx', 'a1,a5,a9,ad,b1,b5,b9,bd'],
    ['LDX', 'imm,zp,abs,zpy,aby', 'a2,a6,ae,b6,be'], ['LDY', 'imm,zp,abs,zpx,abx', 'a0,a4,ac,b4,bc'],
    ['STA', 'inx,zp,abs,iny,zpx,aby,abx', '81,85,8d,91,95,99,9d'],
    ['STX', 'zp,abs,zpy', '86,8e,96'], ['STY', 'zp,abs,zpx', '84,8c,94'],
    ['AND', 'inx,zp,imm,abs,iny,zpx,aby,abx', '21,25,29,2d,31,35,39,3d'],
    ['ORA', 'inx,zp,imm,abs,iny,zpx,aby,abx', '01,05,09,0d,11,15,19,1d'],
    ['EOR', 'inx,zp,imm,abs,iny,zpx,aby,abx', '41,45,49,4d,51,55,59,5d'],
    ['ADC', 'inx,zp,imm,abs,iny,zpx,aby,abx', '61,65,69,6d,71,75,79,7d'],
    ['SBC', 'inx,zp,imm,abs,iny,zpx,aby,abx', 'e1,e5,e9,ed,f1,f5,f9,fd'],
    ['CMP', 'inx,zp,imm,abs,iny,zpx,aby,abx', 'c1,c5,c9,cd,d1,d5,d9,dd'],
    ['CPX', 'imm,zp,abs', 'e0,e4,ec'], ['CPY', 'imm,zp,abs', 'c0,c4,cc'], ['BIT', 'zp,abs', '24,2c'],
    ['ASL', 'zp,acc,abs,zpx,abx', '06,0a,0e,16,1e'], ['ROL', 'zp,acc,abs,zpx,abx', '26,2a,2e,36,3e'],
    ['LSR', 'zp,acc,abs,zpx,abx', '46,4a,4e,56,5e'], ['ROR', 'zp,acc,abs,zpx,abx', '66,6a,6e,76,7e'],
    ['DEC', 'zp,abs,zpx,abx', 'c6,ce,d6,de'], ['INC', 'zp,abs,zpx,abx', 'e6,ee,f6,fe'],
    ['JMP', 'abs', '4c'], ['JSR', 'abs', '20'],
]) codes.split(',').forEach((code, index) => opcodes.set(parseInt(code, 16), [operation, modes.split(',')[index]]));
for (const entry of '10:BPL 30:BMI 50:BVC 70:BVS 90:BCC b0:BCS d0:BNE f0:BEQ e8:INX ca:DEX c8:INY 88:DEY aa:TAX a8:TAY 8a:TXA 98:TYA 48:PHA 68:PLA 08:PHP 28:PLP 18:CLC 38:SEC 78:SEI 58:CLI d8:CLD f8:SED 60:RTS ea:NOP'.split(' ')) {
    const [code, operation] = entry.split(':');
    opcodes.set(parseInt(code, 16), [operation]);
}

test('assembled renderer stages colors and preserves live selection', async t => {
    const acme = findAssembler();
    if (!acme) return t.skip('ACME assembler unavailable; set ACME_EXE to run instruction-level checks');
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'teensyrom-color-test-'));
    try {
        const binaryPath = path.join(temporary, 'desktop.bin');
        const symbolsPath = path.join(temporary, 'symbols');
        const result = spawnSync(acme, ['--format', 'plain', '--symbollist', symbolsPath, '--outfile', binaryPath,
            'source/DesktopShellCode.asm'], { cwd: menuDir, encoding: 'utf8', timeout: 30000, windowsHide: true });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stdout + result.stderr);
        const symbols = Object.fromEntries([...fs.readFileSync(symbolsPath, 'utf8').matchAll(/^\s*(\w+)\s*=\s*\$([0-9a-f]+)/gmi)]
            .map(match => [match[1], parseInt(match[2], 16)]));
        const image = fs.readFileSync(binaryPath);
        t.diagnostic(`Assembled desktop: ${image.length} bytes; ${0xa000 - symbols.MainCodeRAMEnd} bytes below BASIC remain`);
        const fresh = () => {
            const memory = Buffer.alloc(65536);
            image.copy(memory, symbols.MainCodeRAMStart);
            return new Cpu6502(memory);
        };

        await t.test('About renders version and credits inside a native bitmap panel', () => {
            const cpu = fresh();
            const lines = [];
            cpu.m.fill(0xa5, 0x0400, 0x0800);
            cpu.hooks.set(symbols.RichText, () => {
                const start = cpu.a | (cpu.y << 8);
                let text = '';
                for (let address = start; cpu.m[address]; address++) {
                    text += String.fromCharCode(cpu.m[address] & 0x7f);
                }
                const x = cpu.m[symbols.RichX] | (cpu.m[symbols.RichXHi] << 8);
                const y = cpu.m[symbols.RichY];
                assert.ok(x >= 41 && x + text.length * 6 <= 279, `${text} fits horizontally`);
                assert.ok(y >= 49 && y + 7 <= 151, `${text} fits vertically`);
                lines.push(text);
            });
            cpu.onWrite = address => {
                assert.ok(address < 0x0400 || address >= 0x0800, 'About preserves live colors and sprite pointers');
            };
            cpu.call(symbols.GeosRichAbout);
            assert.deepEqual(lines, [
                'MPE FIRMWARE V1.0.1', 'JOHN SWIDERSKI', 'MEAN HAMSTER SOFTWARE',
                'BASED ON TEENSYROM+', 'RETURN / STOP / CLICK TO CLOSE',
            ]);
            assert.ok(cpu.m.subarray(0xa000, 0xbf40).some(value => value !== 0), 'native bitmap contains the panel');
            assert.deepEqual(cpu.m.subarray(0x0400, 0x0800), Buffer.alloc(1024, 0xa5));
        });

        await t.test('About consumes shortcuts and navigation until keyboard, fire, or click closes it', () => {
            const about = () => {
                const cpu = fresh();
                cpu.m[symbols.GeosShellRedraw] = 0x60;
                cpu.m[symbols.GeosViewMode] = 1;
                cpu.m[symbols.GeosHomeSelection] = 3;
                cpu.call(symbols.GeosDeskAbout);
                assert.equal(cpu.m[symbols.GeosOverlayMode], symbols.GeosOverlayAbout);
                return cpu;
            };
            for (const key of ['ChrF1', 'ChrF2', 'ChrF3', 'ChrF4', 'ChrF5', 'ChrF6', 'ChrF7', 'ChrF8']) {
                const cpu = about();
                cpu.a = symbols[key];
                cpu.call(symbols.GeosShellHandleKey);
                assert.equal(cpu.p & 1, 1, `${key} is consumed`);
                assert.equal(cpu.m[symbols.GeosOverlayMode], symbols.GeosOverlayAbout);
            }
            for (const direction of ['Up', 'Down', 'Left', 'Right']) {
                const cpu = about();
                cpu.call(symbols[`GeosShellCursor${direction}`]);
                assert.equal(cpu.p & 1, 1);
                assert.equal(cpu.m[symbols.GeosHomeSelection], 3, `${direction} does not move a covered icon`);
            }
            for (const key of ['ChrReturn', 'ChrStop', 'ChrRun', 'ChrSpace']) {
                const cpu = about();
                cpu.a = symbols[key];
                cpu.call(symbols.GeosShellHandleKey);
                assert.equal(cpu.p & 1, 1);
                assert.equal(cpu.m[symbols.GeosOverlayMode], symbols.GeosOverlayNone, `${key} closes About`);
            }
            const fire = about();
            fire.call(symbols.GeosShellSelectItem);
            assert.equal(fire.m[symbols.GeosOverlayMode], symbols.GeosOverlayNone, 'joystick fire closes About');
            for (const [x, y] of [[0, 0], [20, 12], [39, 24]]) {
                const cpu = about();
                cpu.x = x;
                cpu.y = y;
                cpu.call(symbols.GeosShellMouseClick);
                assert.equal(cpu.m[symbols.GeosOverlayMode], symbols.GeosOverlayNone, `click ${x},${y} closes About`);
                assert.equal(cpu.m[symbols.GeosHomeSelection], 3);
            }
        });

        await t.test('publisher copies exactly 1000 color bytes and skips unchanged cells', () => {
            const cpu = fresh();
            cpu.m.fill(0xa5, 0x0400, 0x0800);
            cpu.m.fill(0x5a, 0x4000, 0x4400);
            const expected = Buffer.from(Array.from({ length: 1000 }, (_, index) => index % 128));
            expected.copy(cpu.m, 0x4000);
            const sourceBefore = Buffer.from(cpu.m.subarray(0x4000, 0x4400));
            let writes = 0;
            cpu.onWrite = address => {
                if (address >= 0x0400 && address < 0x0800) {
                    assert.ok(address < 0x07e8, `publisher must preserve screen guard $${address.toString(16)}`);
                    writes++;
                }
            };
            cpu.m[symbols.GeosBitmapColorOffset] = 0x3c;
            cpu.call(symbols.GeosBitmapPublishColors);
            assert.equal(writes, 1000);
            assert.deepEqual(cpu.m.subarray(0x0400, 0x07e8), expected);
            assert.deepEqual(cpu.m.subarray(0x07e8, 0x0800), Buffer.alloc(24, 0xa5));
            assert.deepEqual(cpu.m.subarray(0x4000, 0x4400), sourceBefore);
            assert.equal(cpu.m[symbols.GeosBitmapColorOffset], 0);
            writes = 0;
            cpu.call(symbols.GeosBitmapPublishColors);
            assert.equal(writes, 0, 'unchanged colors do not get republished');
        });

        await t.test('all 19 two-row labels use staging during composition and live RAM afterward', () => {
            for (const offset of [0, 0x3c]) for (let item = 0; item < 19; item++) {
                const cpu = fresh();
                cpu.m.fill(0xa5, 0x0400, 0x0800);
                cpu.m.fill(0x5a, 0x4000, 0x4400);
                cpu.m[symbols.GeosBitmapColorOffset] = offset;
                const row = cpu.m[symbols.TblGeosCellRow + item] + 2;
                const column = cpu.m[symbols.TblGeosCellCol + item];
                const start = 0x0400 + (offset << 8) + row * 40 + column;
                const expected = new Set(Array.from({ length: 16 }, (_, index) => start + (index < 8 ? index : 40 + index - 8)));
                const actual = new Set();
                cpu.onWrite = (address, value) => {
                    if ((address >= 0x0400 && address < 0x0800) || (address >= 0x4000 && address < 0x4400)) {
                        assert.equal(value, 0x16);
                        actual.add(address);
                    }
                };
                cpu.a = item;
                cpu.x = 0x16;
                cpu.call(symbols.GeosBitmapSetItemLabelColor);
                assert.deepEqual(actual, expected, `item ${item}, offset $${offset.toString(16)}`);
                assert.equal(cpu.m[symbols.GeosBitmapColorOffset], offset);
            }
        });

        await t.test('browser frame and four native gadgets match every pixel without touching colors', () => {
            const cpu = fresh();
            cpu.m.fill(0xa5, 0x0400, 0x0800);
            cpu.m.fill(0x5a, 0x4000, 0x4400);
            const wanted = new Uint8Array(320 * 200);
            for (const y of [15, 23, 183]) for (let x = 0; x < 320; x++) wanted[y * 320 + x] = 1;
            for (let y = 8; y < 184; y++) wanted[y * 320] = wanted[y * 320 + 319] = 1;
            for (const [name, left, top, columns] of [
                ['RichBrowserClose', 0, 8, 3], ['RichBrowserUp', 0, 16, 4],
                ['RichBrowserPrev', 200, 8, 2], ['RichBrowserNext', 304, 8, 2],
            ]) for (let y = 0; y < 8; y++) for (let byte = 0; byte < columns; byte++) {
                const bits = cpu.m[symbols[name] + y * columns + byte];
                for (let bit = 0; bit < 8; bit++) if (bits & (128 >> bit)) wanted[(top + y) * 320 + left + byte * 8 + bit] = 1;
            }
            cpu.onWrite = address => {
                assert.ok(address < 0x0400 || address >= 0x0800, 'chrome does not write live colors or sprite pointers');
                assert.ok(address < 0x4000 || address >= 0x4800, 'chrome preserves staged colors and font');
            };
            cpu.call(symbols.GeosRichBrowserChrome);
            for (let y = 0; y < 200; y++) for (let x = 0; x < 320; x++) {
                const address = 0xa000 + Math.floor(y / 8) * 320 + Math.floor(x / 8) * 8 + y % 8;
                const actual = (cpu.m[address] >> (7 - x % 8)) & 1;
                assert.equal(actual, wanted[y * 320 + x], `chrome pixel ${x},${y}`);
            }
        });

        await t.test('only visible previous/next gadgets page in both file backends', () => {
            for (const entry of ['GeosMouseBrowserPage', 'GeosIECMousePage']) for (let column = 0; column <= 40; column++) {
                const cpu = fresh();
                cpu.x = column;
                cpu.y = 1;
                cpu.m[symbols.MouseOpenArmed] = 1;
                cpu.call(symbols[entry]);
                const previous = column >= 25 && column < 27;
                const next = column >= 38 && column < 40;
                assert.equal(Boolean(cpu.p & 1), previous || next, `${entry}, column ${column}`);
                if (previous || next) assert.equal(cpu.a, symbols[previous ? 'MouseEventPagePrev' : 'MouseEventPageNext']);
                assert.equal(cpu.m[symbols.MouseOpenArmed], 0);
            }
        });

        await t.test('close and parent gadget boundaries agree for Teensy and IEC windows', () => {
            for (const surface of [1, 2]) for (const row of [1, 2]) for (let column = 0; column < 40; column++) {
                const cpu = fresh();
                cpu.x = column;
                cpu.y = row;
                cpu.m[symbols.GeosSurfaceMode] = surface;
                cpu.m[symbols.GeosOverlayMode] = 0;
                // Only the already-tested desktop redraw/service boundary is
                // replaced with RTS; run the actual common mouse router.
                cpu.m[symbols.GeosFileDesktop] = 0x60;
                let closed = false;
                cpu.hooks.set(symbols.GeosFileDesktop, () => { closed = true; });
                cpu.call(symbols.GeosShellMouseClick);
                assert.equal(closed, row === 1 && column < 3, `surface ${surface}, ${column},${row}`);
                if (row === 2) {
                    assert.equal(Boolean(cpu.p & 1), column < 4);
                    if (column < 4) assert.equal(cpu.a, symbols.ChrUpArrow);
                }
            }
        });

        for (const [name, surface, overlay, activeMenu] of [
            ['home', 0, 0, 0], ['home menu', 0, 1, 1], ['home control panel', 0, 2, 0],
            ['home About', 0, 4, 0], ['browser About', 1, 4, 0],
            ['browser', 1, 0, 0], ['browser menu', 1, 1, 4], ['IEC browser', 2, 0, 0], ['IEC menu', 2, 1, 2],
        ]) await t.test(`${name}: no visible palette write until all 8000 bitmap bytes are published`, () => {
            const cpu = fresh();
            cpu.m[1] = 0x37;
            cpu.m[symbols.GeosSurfaceMode] = surface;
            cpu.m[symbols.GeosOverlayMode] = overlay;
            cpu.m[symbols.GeosActiveMenu] = activeMenu;
            cpu.m[symbols.GeosBitmapLayoutPass] = 1;
            cpu.m[symbols.GeosViewMode] = 1;
            cpu.m[symbols.GeosIECCount] = 19;
            cpu.m[symbols.GeosIECSelection] = 18;
            cpu.m[symbols.rRegNumItemsOnPage + symbols.IO1Port] = 19;
            cpu.m[symbols.rwRegCursorItemOnPg + symbols.IO1Port] = 18;
            cpu.m[symbols.TODHoursBCD] = 0x10;
            cpu.m[symbols.smc24HourClockDisp + 1] = 0;
            cpu.m.fill(0xa5, 0x0400, 0x0800);
            cpu.m.fill(0x3c, 0x2000, 0x3f40);
            for (let index = 0; index < 1000; index++) cpu.m[0x4000 + index] = (index * 7) & 255;
            for (let index = 0; index < 1024; index++) cpu.m[0x4400 + index] = (index * 13) & 255;
            for (let item = 0; item < 19; item++) Buffer.from(`FILE ${item} SECOND ROW\0`).copy(cpu.m, symbols.GeosRichFileLabels + item * 21);
            const fontBefore = Buffer.from(cpu.m.subarray(0x4400, 0x4800));
            let bitmapPublished = false;
            let publishCalls = 0;
            let finalColors;
            let finalBitmap;
            cpu.hooks.set(symbols.GeosRichPublish, () => {
                assert.deepEqual(cpu.m.subarray(0x0400, 0x07e8), Buffer.alloc(1000, 0xa5));
                finalColors = Buffer.from(cpu.m.subarray(0x4000, 0x43e8));
                finalBitmap = Buffer.from(cpu.m.subarray(0xa000, 0xbf40));
            });
            cpu.hooks.set(symbols.GeosBitmapPublishColors, () => {
                assert.ok(finalBitmap, 'bitmap publication occurs first');
                assert.deepEqual(cpu.m.subarray(0x2000, 0x3f40), finalBitmap);
                bitmapPublished = true;
                publishCalls++;
            });
            cpu.onWrite = address => {
                if (address >= 0x0400 && address < 0x07e8) assert.ok(bitmapPublished, `early color write $${address.toString(16)}`);
                if (address >= 0x07e8 && address < 0x0800) assert.equal(address, symbols.Sprite0Pointer, 'only the explicit pointer setup may touch screen padding');
            };
            cpu.call(symbols.GeosBitmapConvertScreen);
            assert.equal(publishCalls, 1);
            assert.deepEqual(cpu.m.subarray(0x0400, 0x07e8), finalColors);
            assert.deepEqual(cpu.m.subarray(0x2000, 0x3f40), finalBitmap);
            assert.deepEqual(cpu.m.subarray(0x4400, 0x4800), fontBefore);
            assert.equal(cpu.m[symbols.GeosBitmapColorOffset], 0);
            assert.equal(cpu.m[1], 0x37, 'BASIC bank restored');
            assert.equal(cpu.m[0xd011] & 0x60, 0x20, 'standard bitmap mode');
            assert.equal(cpu.m[0xd016] & 0x10, 0, 'multicolor stays off');
        });
    } finally {
        // The target is the exact directory created by mkdtemp above.
        fs.rmSync(temporary, { recursive: true, force: true });
    }
});
