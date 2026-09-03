'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { desktopMachine } = require('./desktop-machine');

test('startup firmware discovery uses the shared guarded confirmation without changing the browser', t => desktopMachine(t, async ({ s, fresh, stub, menuDir }) => {
    const fixture = ({ ready = 1, changed = false, active = 1, answers = [13] } = {}) => {
        const cpu = fresh(), calls = [], glyphs = [], events = [...answers];
        const candidate = 'MPE_Firmware-V1.0.10.hex';
        let serial = 0;
        cpu.m[s.GeosBitmapActive] = active;
        cpu.m[s.GeosSurfaceMode] = s.GeosSurfaceHome;
        cpu.m[s.IO1Port + s.rWRegCurrMenuWAIT] = s.rmtTeensy;
        cpu.m[s.IO1Port + s.rwRegCursorItemOnPg] = 7;
        cpu.m[s.IO1Port + s.rwRegSelItemOnPage] = 4;
        cpu.onWrite = (address, value) => {
            if (address === s.IO1Port + s.wRegControl) calls.push(value);
            if (address === s.IO1Port + s.rwRegSerialString) {
                assert.equal(value, s.rsstFirmwareName); serial = 0;
            }
            assert.notEqual(address, s.IO1Port + s.rWRegCurrMenuWAIT, 'discovery does not navigate');
            assert.notEqual(address, s.IO1Port + s.rwRegSelItemOnPage, 'discovery does not select another browser item');
        };
        const step = cpu.step.bind(cpu);
        cpu.step = () => {
            const address = cpu.m[cpu.pc + 1] | cpu.m[cpu.pc + 2] << 8;
            if (cpu.m[cpu.pc] === 0xad && address === s.IO1Port + s.rwRegSerialString)
                cpu.m[address] = candidate.charCodeAt(serial++) || 0;
            step();
        };
        stub(cpu, 'WaitForTRWaitMsg', c => {
            c.m[s.IO1Port + s.rRegFirmwareTargetState] = changed && calls.at(-1) === s.rCtlFirmwareCheckWAIT ? 2 : ready;
        });
        stub(cpu, 'GetIn', c => {
            assert.ok(events.length, 'confirmation must not consume an unplanned key');
            c.a = c.nz(events.shift());
        });
        for (const label of ['IRQDisable', 'StartSelItem_WaitForTRDots', 'AnyKeyErrMsgWait', 'ListAndDone'])
            stub(cpu, label, () => calls.push(label));
        cpu.hooks.set(s.GeosDialogGlyph, c => { if (c.m[s.RichY] < 120) glyphs.push(c.a); });
        cpu.call(s.GeosFirmwareStartup);
        assert.equal(cpu.m[s.GeosSurfaceMode], s.GeosSurfaceHome);
        assert.equal(cpu.m[s.IO1Port + s.rWRegCurrMenuWAIT], s.rmtTeensy);
        assert.equal(cpu.m[s.IO1Port + s.rwRegCursorItemOnPg], 7);
        assert.equal(cpu.m[s.IO1Port + s.rwRegSelItemOnPage], 4);
        return { calls, glyphs: Buffer.from(glyphs).toString('ascii'), candidate };
    };
    await t.test('no candidate dismisses loading without prompting; recovery text mode is untouched', () => {
        for (const ready of [0, 2, 3])
            assert.deepEqual(fixture({ ready, answers: [] }).calls, [s.rCtlFirmwareDiscoverWAIT, s.rCtlFirmwareCancel, 'ListAndDone']);
        assert.deepEqual(fixture({ active: 0, answers: [] }).calls, []);
    });
    await t.test('Cancel is the default and never disables interrupts or starts an update', () => {
        for (const answer of [s.ChrReturn, s.ChrStop, 0x4e, 0xce]) {
            const result = fixture({ answers: [answer] });
            assert.deepEqual(result.calls, [s.rCtlFirmwareDiscoverWAIT, s.rCtlFirmwareCancel, 'ListAndDone']);
            assert.equal(result.glyphs, result.candidate, 'full exact candidate displayed');
        }
    });
    await t.test('only an affirmative after display can check and begin the captured update', () => {
        for (const answers of [[0x59], [s.ChrCRSRRight, s.ChrReturn]]) {
            const result = fixture({ answers });
            assert.equal(result.glyphs, result.candidate);
            assert.deepEqual(result.calls, [s.rCtlFirmwareDiscoverWAIT, s.rCtlFirmwareCheckWAIT,
                'IRQDisable', 'StartSelItem_WaitForTRDots', 'AnyKeyErrMsgWait', s.rCtlFirmwareCancel, 'ListAndDone']);
        }
    });
    await t.test('a changed candidate after confirmation cannot reach the flasher', () => {
        const result = fixture({ changed: true, answers: [0x59, s.ChrReturn] });
        assert.deepEqual(result.calls, [s.rCtlFirmwareDiscoverWAIT, s.rCtlFirmwareCheckWAIT, s.rCtlFirmwareCancel, 'ListAndDone']);
        assert.ok(result.glyphs.includes('Firmware selection changed. Choose the file again.'));
    });
    await t.test('discovery has one startup call after desktop drawing, outside the input loop', () => {
        const source = fs.readFileSync(path.join(menuDir, 'source/MainMenu.asm'), 'utf8');
        assert.equal((source.match(/jsr GeosFirmwareStartup/g) || []).length, 1);
        assert.match(source, /jsr ListMenuItems\s+!ifdef DesktopShell \{\s+jsr GeosFirmwareStartup\s+\}\s+HighlightCurrent:/);
    });
}));
