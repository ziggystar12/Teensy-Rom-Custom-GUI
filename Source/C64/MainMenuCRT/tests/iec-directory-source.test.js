'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = name => fs.readFileSync(path.join(__dirname, '../source', name), 'utf8');
const io = source('GeosIECIO.s');
const ui = source('GeosIEC.s');
const shell = source('GeosShell.s');

test('IEC browser reads actual channel APIs for devices 8 and 9, not Teensy storage', () => {
  assert.match(io, /GeosIECKernalOPEN = \$ffc0/);
  assert.match(io, /GeosIECKernalCHRIN = \$ffcf/);
  assert.match(io, /lda GeosIECDevice\s+cmp #8[\s\S]*cmp #9/);
  assert.doesNotMatch(io, /IO1Port|rmtSD|rmtUSBDrive/);
  assert.match(io, /GeosIECPageSize = 19/);
});

test('IEC input preserves SID state, handles the final EOI byte and bounds parsing', () => {
  assert.match(io, /lda smcSIDPauseStop\+1\s+sta GeosIECSavedSID/);
  assert.match(io, /lda GeosIECSavedSID\s+sta smcSIDPauseStop\+1/);
  assert.match(io, /and #\$bf\s+bne GeosIECIOError/);
  assert.match(io, /and #\$40\s+sta GeosIECEOF\s+lda GeosIECReadValue\s+clc/);
  assert.match(io, /lda #96\s+sta GeosIECLineRemaining/);
  assert.match(io, /lda GeosIECBytesHi\s+bmi GeosIECLimitOrStop/);
  assert.match(io, /jsr GeosIECKernalSTOP/);
  assert.match(io, /cmp \$0259,x/); // Never close another program's logical file.
});

test('IEC view only navigates directories and disk images and prevents stale Teensy actions', () => {
  assert.match(ui, /GeosIECActivate:[\s\S]*jsr GeosIECEntryIsDirectory/);
  assert.match(ui, /lda #\$43\s+sta GeosIECCommand\s+lda #\$44/);
  assert.match(ui, /lda #\$5f\s+;[^\n]*\n\s*sta GeosIECCommand\+3/);
  assert.match(io, /Only directory changes are accepted/);
  assert.doesNotMatch(io + ui, /\$ffd5|\$ffd8|rCtlStartSelItemWAIT|rCtlMountDxxFileWAIT/);
  assert.match(shell, /cmp #GeosSurfaceIEC\s+bne \+\s+lda GeosShellKey\s+jsr GeosIECHandleKey/);
  assert.match(ui, /GeosIECConsumeKey:\s+sec\s+rts/);
});

test('IEC records and selection stay in the protected payload, separate from bitmap scratch', () => {
  assert.match(ui, /php\s+sei[\s\S]*GeosIECEntryCopy:[\s\S]*plp/);
  assert.match(source('GeosBitmap.s'), /GeosBitmapFontData = \$4400/);
  assert.match(source('GeosDesktop.s'), /GeosLayoutScreen = \$4000/);
  assert.match(source('MainMenu.asm'), /MainCodeRAMEnd > \$a000/);
});
