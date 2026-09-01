'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const sourceDir = path.join(__dirname, '..', 'source');
const desktop = fs.readFileSync(path.join(sourceDir, 'GeosDesktop.s'), 'utf8');
const common = fs.readFileSync(path.join(sourceDir, 'CommonDefs.i'), 'utf8');
const main = fs.readFileSync(path.join(sourceDir, 'MainMenu.asm'), 'utf8');
const mouse = fs.readFileSync(path.join(sourceDir, 'Mouse1351.s'), 'utf8');
const parser = fs.readFileSync(
    path.join(__dirname, '..', '..', '..', 'Teensy', 'FileParsers.ino'),
    'utf8',
);

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
    assert.match(desktop, /lda #\$1f\s+;screen \$0400, charset \$3800/);
});

test('RAM charset is included in the menu SID-protection range', () => {
    assert.match(common, /MenuReservedRAMStart = \$3800/);
    assert.match(main, /lda #>MainCodeRAMStart/);
    assert.match(desktop, /GeosCharsetRAM = MenuReservedRAMStart/);
    assert.match(parser, /LoadAddress < 0x4000.*LoadAddress\+XferSize >= 0x3800/);
});

test('the proven mouse-port-1 and joystick-port-2 mapping remains unchanged', () => {
    assert.match(mouse, /mouse is read from control port 1/i);
    assert.match(main, /;Check joystick first:\s+lda CIA1_RegA/);
    assert.doesNotMatch(mouse, /MousePort2|MouseJoy1/);
});
