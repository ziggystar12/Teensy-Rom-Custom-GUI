'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const sourceDir = path.join(__dirname, '..', 'source');
const desktop = fs.readFileSync(path.join(sourceDir, 'GeosDesktop.s'), 'utf8');
const bitmap = fs.readFileSync(path.join(sourceDir, 'GeosBitmap.s'), 'utf8');
const common = fs.readFileSync(path.join(sourceDir, 'CommonDefs.i'), 'utf8');
const main = fs.readFileSync(path.join(sourceDir, 'MainMenu.asm'), 'utf8');
const mouse = fs.readFileSync(path.join(sourceDir, 'Mouse1351.s'), 'utf8');
const strings = fs.readFileSync(path.join(sourceDir, 'StringFunctions.s'), 'utf8');
const parser = fs.readFileSync(
    path.join(__dirname, '..', '..', '..', 'Teensy', 'FileParsers.ino'),
    'utf8',
);
const stripComments = (source) => source.replace(/;[^\r\n]*/g, '');
const bitmapCode = stripComments(bitmap);
const desktopCode = stripComments(desktop);
const mainCode = stripComments(main);
const stringsCode = stripComments(strings);

function sourceBlock(source, startLabel, endLabel) {
    const start = source.indexOf(startLabel);
    const end = source.indexOf(endLabel, start + startLabel.length);
    assert.notEqual(start, -1, `${startLabel} exists`);
    assert.notEqual(end, -1, `${endLabel} exists after ${startLabel}`);
    return source.slice(start, end);
}

function immediateStoredIn(source, target) {
    const escapedTarget = target.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const match = source.match(
        new RegExp(`lda\\s+#\\$([0-9a-f]{2})\\s+sta\\s+${escapedTarget}`, 'i'),
    );
    assert.ok(match, `an immediate byte is stored in ${target}`);
    return Number.parseInt(match[1], 16);
}

function tableAddresses(source, startLabel, endLabel, prefix) {
    const block = sourceBlock(source, startLabel, endLabel);
    const pattern = new RegExp(`${prefix}\\$([0-9a-f]{4})`, 'gi');
    return [...block.matchAll(pattern)].map((match) => Number.parseInt(match[1], 16));
}

test('monochrome desktop carries four complete 24x16 pixel icons', () => {
    const iconBlock = desktop.slice(
        desktop.indexOf('GeosIconData:'),
        desktop.indexOf('GeosIconDataEnd:'),
    );
    const bytes = [...iconBlock.matchAll(/%([01]{8})/g)].map((match) =>
        Number.parseInt(match[1], 2),
    );

    assert.equal(bytes.length, 4 * 6 * 8);
    for (let icon = 0; icon < 4; icon += 1) {
        const iconBytes = bytes.slice(icon * 48, icon * 48 + 48);
        assert.ok(iconBytes.some((value) => value !== 0), `icon ${icon} has pixels`);
    }
    assert.match(desktop, /GeosIconProgram = GeosIconDocument\+6/);
});

test('expanded shell presents a true 320x200 standard high-resolution bitmap', () => {
    assert.match(bitmapCode, /GeosBitmapRAM\s*=\s*\$2000/);
    assert.match(bitmapCode, /GeosBitmapRAMEnd\s*=\s*\$3f40/);
    assert.match(bitmapCode, /GeosBitmapScreen\s*=\s*C64ScreenRAM/);

    const conversion = sourceBlock(
        bitmapCode,
        'GeosBitmapConvertScreen:',
        'GeosBitmapCaptureFont:',
    );
    assert.match(conversion, /cmp\s+#40[\s\S]*cmp\s+#25/);
    const memorySetup = immediateStoredIn(conversion, 'VICMemSetup');
    const control2 = immediateStoredIn(conversion, '$d016');
    const control1 = immediateStoredIn(conversion, '$d011');
    assert.equal(memorySetup, 0x18); // screen $0400, bitmap $2000
    assert.equal(control2 & 0x10, 0); // MCM off: one bit per 320-wide pixel
    assert.equal(control2 & 0x08, 0x08); // 40 columns
    assert.equal(control1 & 0x20, 0x20); // BMM on
    assert.equal(control1 & 0x40, 0); // ECM off

    const expectedBitmapRows = Array.from({ length: 25 }, (_, row) =>
        0x2000 + row * 0x140,
    );
    const expectedScreenRows = Array.from({ length: 25 }, (_, row) =>
        0x0400 + row * 40,
    );
    assert.deepEqual(
        tableAddresses(bitmapCode, 'TblGeosBitmapRowLo:', 'TblGeosBitmapRowHi:', '<'),
        expectedBitmapRows,
    );
    assert.deepEqual(
        tableAddresses(
            bitmapCode,
            'TblGeosBitmapRowHi:',
            'TblGeosBitmapScreenRowLo:',
            '>',
        ),
        expectedBitmapRows,
    );
    assert.deepEqual(
        tableAddresses(
            bitmapCode,
            'TblGeosBitmapScreenRowLo:',
            'TblGeosBitmapScreenRowHi:',
            '<',
        ),
        expectedScreenRows,
    );
    assert.deepEqual(
        tableAddresses(
            bitmapCode,
            'TblGeosBitmapScreenRowHi:',
            'GeosBitmapActive:',
            '>',
        ),
        expectedScreenRows,
    );
});

test('each 8x8 bitmap cell receives one foreground/background screen byte', () => {
    const expectedColors = {
        Normal: 0x01,
        Accent: 0x61,
        Selected: 0x16,
        Clock: 0x76,
        Status: 0x0f,
    };
    for (const [name, expected] of Object.entries(expectedColors)) {
        const match = bitmapCode.match(
            new RegExp(`GeosBitmapColor${name}\\s*=\\s*\\$([0-9a-f]{2})`, 'i'),
        );
        assert.ok(match, `${name} color pair exists`);
        const colorByte = Number.parseInt(match[1], 16);
        assert.equal(colorByte, expected);
        assert.notEqual(colorByte >> 4, colorByte & 0x0f);
    }
    assert.match(
        bitmapCode,
        /GeosBitmapCopyGlyph:[\s\S]*cpy #8[\s\S]*GeosBitmapStoreCellColor:[\s\S]*smcGeosBitmapWriteCell:\s+sta \$ffff,x/,
    );
    assert.doesNotMatch(bitmapCode, /\$d800|C64ColorRAM/);
});

test('bitmap address math retains column times eight carry for columns 32 through 39', () => {
    const pointer = sourceBlock(
        bitmapCode,
        'GeosBitmapSetCellPointer:',
        'GeosBitmapTintSurface:',
    );
    assert.match(
        pointer,
        /sta GeosBitmapCellOffsetHi[\s\S]*asl\s+rol GeosBitmapCellOffsetHi[\s\S]*asl\s+rol GeosBitmapCellOffsetHi[\s\S]*asl\s+rol GeosBitmapCellOffsetHi/,
    );
    assert.match(
        pointer,
        /adc Ptr2AddrLo\s+sta Ptr2AddrLo\s+lda GeosBitmapCellOffsetHi\s+adc Ptr2AddrHi\s+sta Ptr2AddrHi/,
    );
});

test('SID IRQs cannot clobber zero-page bitmap pointers during glyph copies', () => {
    const conversion = sourceBlock(
        bitmapCode,
        'GeosBitmapConvertCell:',
        'GeosBitmapCellSelected:',
    );
    const putChar = sourceBlock(
        bitmapCode,
        'GeosBitmapPutChar:',
        'GeosBitmapPetsciiToScreen:',
    );
    for (const block of [conversion, putChar]) {
        assert.match(
            block,
            /php\s+sei[\s\S]*jsr GeosBitmapSetFontPointer[\s\S]*jsr GeosBitmapSetCellPointer[\s\S]*cpy #8[\s\S]*plp/,
        );
    }
});

test('bitmap live text reaches the printable glyph path', () => {
    const putChar = sourceBlock(
        bitmapCode,
        'GeosBitmapPutChar:',
        'GeosBitmapPetsciiToScreen:',
    );
    assert.match(
        putChar,
        /cmp #ChrReturn\s+bne GeosBitmapPutPrintable[\s\S]*GeosBitmapPutPrintable:\s+jsr GeosBitmapPetsciiToScreen/,
    );
});

test('all 256 temporary glyphs are captured before bitmap conversion overwrites them', () => {
    const conversion = sourceBlock(
        bitmapCode,
        'GeosBitmapConvertScreen:',
        'GeosBitmapCaptureFont:',
    );
    assert.match(conversion, /jsr\s+GeosBitmapCaptureFont/);
    const capture = sourceBlock(
        bitmapCode,
        'GeosBitmapCaptureFont:',
        'GeosBitmapSetFontPointer:',
    );
    for (let page = 0; page < 8; page += 1) {
        const offset = (page * 0x100).toString(16).padStart(3, '0');
        assert.match(
            capture,
            new RegExp(
                `lda\\s+GeosCharsetRAM\\+\\$${offset},x\\s+sta\\s+GeosBitmapFontData\\+\\$${offset},x`,
                'i',
            ),
        );
    }
    assert.match(bitmapCode, /GeosBitmapFontData:\s*!fill\s+\$800,0/);
});

test('bitmap display RAM is included in the menu SID-protection range', () => {
    assert.match(common, /MenuReservedRAMStart = \$2000/);
    assert.match(main, /lda #>MainCodeRAMStart/);
    assert.match(desktop, /GeosCharsetRAM = \$3800/);
    assert.match(parser, /LoadAddress < 0x4000.*LoadAddress\+XferSize >= 0x2000/);
});

test('compact cartridge and classic list retain the character-mode fallback', () => {
    assert.match(
        mainCode,
        /ListMenuItems:\s+lda GeosViewMode\s+beq ListMenuItemsClassic\s+jmp GeosDrawDesktop/,
    );
    const textMode = sourceBlock(mainCode, 'TextScreenMemColor:', 'ScreenColorOnly:');
    assert.match(textMode, /lda\s+#0\s+sta\s+GeosBitmapActive/);
    assert.equal(immediateStoredIn(textMode, '$d018'), 0x17);
    assert.equal(immediateStoredIn(textMode, '$d016'), 0xc8);
    assert.equal(immediateStoredIn(textMode, '$d011') & 0x20, 0);
    assert.match(
        desktopCode,
        /GeosInstallMonoCharset:[\s\S]*lda\s+#\$1f[\s\S]*sta\s+VICMemSetup/,
    );
    assert.match(
        mainCode,
        /!src "source\/GeosDesktop\.s"\s*!ifdef DesktopShell \{\s*!src "source\/GeosShell\.s"\s*!src "source\/GeosBitmap\.s"\s*\}/,
    );
    const banner = sourceBlock(stringsCode, 'PrintBanner:', 'DisplayTime:');
    assert.match(banner, /jsr\s+TextScreenMemColor[\s\S]*jsr\s+PrintString/);
});

test('expanded desktop redraws are converted only after character layout completes', () => {
    assert.match(
        desktopCode,
        /GeosDrawDesktop:\s*!ifdef DesktopShell \{[\s\S]*jsr GeosShellDrawHome\s+jmp GeosBitmapConvertScreen/,
    );
    assert.match(
        desktopCode,
        /GeosItemsDone:\s+jsr GeosDrawStatus\s*!ifdef DesktopShell \{\s+jsr GeosShellDrawOverlay\s+jmp GeosBitmapConvertScreen/,
    );
    assert.match(
        stringsCode,
        /DisplayTime:\s*!ifdef DesktopShell \{\s+lda GeosBitmapActive\s+beq \+\s+jmp GeosBitmapDisplayTime/,
    );
});

test('the proven mouse-port-1 and joystick-port-2 mapping remains unchanged', () => {
    assert.match(mouse, /mouse is read from control port 1/i);
    assert.match(main, /;Check joystick first:\s+lda CIA1_RegA/);
    assert.doesNotMatch(mouse, /MousePort2|MouseJoy1/);
});
