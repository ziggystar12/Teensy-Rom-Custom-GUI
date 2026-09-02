'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const sourceDir = path.join(__dirname, '..', 'source');
const repoRoot = path.resolve(__dirname, '../../../..');
const shell = fs.readFileSync(path.join(sourceDir, 'GeosShell.s'), 'utf8');
const main = fs.readFileSync(path.join(sourceDir, 'MainMenu.asm'), 'utf8');
const wrapper = fs.readFileSync(path.join(sourceDir, 'DesktopShell.asm'), 'utf8');
const payload = fs.readFileSync(path.join(sourceDir, 'DesktopShellCode.asm'), 'utf8');
const strings = fs.readFileSync(path.join(sourceDir, 'StringFunctions.s'), 'utf8');
const mouse = fs.readFileSync(path.join(sourceDir, 'Mouse1351.s'), 'utf8');
const items = fs.readFileSync(path.join(repoRoot, 'Source/Teensy/MainMenuItems.h'), 'utf8');
const firmware = fs.readFileSync(path.join(repoRoot, 'Source/Teensy/TeensyROM.h'), 'utf8');

function sourceBlock(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);

  assert.notEqual(start, -1, `missing start marker: ${startMarker}`);
  assert.notEqual(end, -1, `missing end marker: ${endMarker}`);
  return source.slice(start, end);
}

test('the compact cartridge boots the flash-backed standalone desktop', () => {
  assert.match(payload, /!set DesktopShell=1/);
  assert.match(wrapper, /\* = \$0801[\s\S]*!binary "build\/DesktopShellCode\.bin"/);
  assert.match(main, /!ifndef DesktopShell[\s\S]*ldx #9[\s\S]*lda #3[\s\S]*jmp DirectRunFromTeensyMenu/);
  assert.match(main, /DirectRunFromTeensyMenu:[\s\S]*lda #GeosSurfaceBrowser[\s\S]*sta GeosOverlayMode[\s\S]*jsr SelectItem[\s\S]*jsr SelectItem/);
  assert.match(items, /\/\*3\*\/rtFilePrg[\s\S]*"TeensyROM Desktop Shell"[\s\S]*DesktopShell_prg/);
  assert.match(firmware, /#include "TRMenuFiles\/ROMs\/DesktopShell\.prg\.h"/);
});

test('desktop exposes the five menus, RTC clock, drives, folders, control panel, and trash', () => {
  assert.match(shell, /"TR DESK FILE EDIT VIEW DISK {13}"/);
  assert.match(shell, /TblGeosMenuCount:\s*!byte 4,4,3,4,5/);
  assert.match(shell, /GeosMouseOpenDesk:\s+lda #GeosMenuDesk\s+jmp GeosMouseOpenMenu/);
  assert.match(shell, /MsgHomeDrive8:[^\n]*"DRIVE 8 "/);
  assert.match(shell, /MsgHomeDrive9:[^\n]*"DRIVE 9 "/);
  assert.match(shell, /MsgHomeGames:[^\n]*" GAMES  "/);
  assert.match(shell, /MsgHomeUtilities:[^\n]*" UTILS  "/);
  assert.match(shell, /MsgHomeControl:[^\n]*"CONTROL "/);
  assert.match(shell, /MsgHomeTrash:[^\n]*" TRASH  "/);
  assert.match(strings, /top-row menu bar[\s\S]*ldy #30/);
});

test('desktop redraw hides only the pointer while true exits clear click state', () => {
  const redrawHide = sourceBlock(mouse, 'Mouse1351HideForRedraw:', 'Mouse1351Hide:');
  assert.match(redrawHide, /lda SpriteEnable\s+and #%11111110\s+sta SpriteEnable\s+rts/);
  assert.doesNotMatch(redrawHide, /MouseMenuEnabled|MouseClickEdge|MouseOpenArmed/);

  const fullHide = sourceBlock(mouse, 'Mouse1351Hide:', '; Called once around');
  assert.match(fullHide, /jsr Mouse1351HideForRedraw/);
  assert.match(
    fullHide,
    /lda #0\s+sta MouseMenuEnabled\s+sta MouseClickEdge\s+sta MouseOpenArmed/,
  );

  const textMode = sourceBlock(main, 'TextScreenMemColor:', 'ScreenColorOnly:');
  assert.match(textMode, /jsr Mouse1351HideForRedraw/);
  assert.doesNotMatch(textMode, /jsr Mouse1351Hide\s/);
  assert.match(main, /RunSelected:\s+jsr Mouse1351Hide\s/);
});

test('mouse pointer remains visible on the light desktop surface', () => {
  assert.match(
    mouse,
    /Mouse1351ShowPointer:[\s\S]*?lda #PokeBlack\s+sta Sprite0Color/,
  );
});

test('expanded browser page clicks align with the visible page field', () => {
  const browserHeader = sourceBlock(
    shell,
    'GeosShellDrawBrowserHeader:',
    'GeosShellDrawBrowserFooter:',
  );
  const pageHit = sourceBlock(mouse, 'MouseHitPageBar:', 'MouseReturnPagePrev:');
  assert.match(browserHeader, /ldx #1\s+ldy #25[\s\S]*lda #<MsgGeosPage/);
  assert.match(
    pageHit,
    /!ifdef DesktopShell \{\s+cpx #25[\s\S]*cpx #30[\s\S]*MouseEventPageNext/,
  );
});

test('clock-adjacent media control uses the established F4 SID toggle', () => {
  const menuHit = sourceBlock(shell, 'GeosMouseMenuBar:', 'GeosMouseDropdown:');
  assert.match(
    menuHit,
    /cpx #28\s+bcc GeosMouseOpenDisk\s+cpx #30\s+bcc GeosMouseToggleSID/,
  );
  assert.match(
    menuHit,
    /GeosMouseToggleSID:\s+lda #ChrF4\s+jmp MouseReturnVirtualKey/,
  );
  assert.match(main, /cmp #ChrF4[\s\S]*jsr ToggleSIDMusic/);
});

test('legacy KERNAL pages leave bitmap mode before printing a banner', () => {
  const banner = sourceBlock(strings, 'PrintBanner:', 'DisplayTime:');
  assert.match(
    banner,
    /sta GeosBitmapLayoutPass[\s\S]*?jsr TextScreenMemColor\s+jsr Mouse1351Hide[\s\S]*?jsr PrintString/,
  );
});

test('legacy WAIT messages also leave bitmap mode before KERNAL output', () => {
  assert.match(
    main,
    /WaitForTRDots:[\s\S]*?jsr GeosBitmapPrepareLegacyWait[\s\S]*?WaitForTRWaitMsg:[\s\S]*?jsr GeosBitmapPrepareLegacyWait/,
  );
});

test('home status uses row 23 so a 40-column clear cannot scroll away the menu bar', () => {
  const homeStatus = sourceBlock(
    shell,
    'GeosShellDrawHomeStatus:',
    'GeosShellPrintNotice:',
  );
  assert.match(homeStatus, /ldx #23\s+jsr GeosBlankLine\s+ldx #23\s+ldy #0/);
  assert.doesNotMatch(homeStatus, /ldx #24/);
});

test('home desktop carries seven complete and distinct 16x16 monochrome icon families', () => {
  const iconBlock = shell.slice(
    shell.indexOf('GeosHomeIconData:'),
    shell.indexOf('GeosHomeIconDataEnd:'),
  );
  const bytes = [...iconBlock.matchAll(/%([01]{8})/g)].map((match) => match[1]);

  assert.equal(bytes.length, 7 * 4 * 8);
  const icons = [];
  for (let icon = 0; icon < 7; icon += 1) {
    const current = bytes.slice(icon * 32, icon * 32 + 32);
    assert.ok(current.some((value) => value !== '00000000'), `icon ${icon} has pixels`);
    icons.push(current.join(''));
  }
  assert.equal(new Set(icons).size, 7);
});

test('control categories route to Settings pages and moved icons persist one snapped slot', () => {
  assert.match(shell, /TblGeosControlPage:\s*!byte 3,1,2,1,5,4,6,0/);
  assert.match(shell, /GeosShellLaunchControlPage:[\s\S]*ora #\$80[\s\S]*sta rwRegScratch\+IO1Port/);
  assert.match(shell, /GeosHomeSlotCount = 15/);
  assert.match(shell, /GeosHomeSlotIsEmpty:[\s\S]*GeosHomeSlotToIcon/);
  assert.match(shell, /GeosShellPersistIcon:[\s\S]*sta rwRegDesktopSlotStart\+IO1Port,x[\s\S]*jsr WaitForTRWaitMsg/);
  assert.match(shell, /lda GeosDragActive\s+beq GeosMouseReleaseWithoutDrag[\s\S]*sta MouseOpenArmed\s+GeosMouseReleaseClearCandidate:/);
  const noDragRelease = sourceBlock(
    shell,
    'GeosMouseReleaseWithoutDrag:',
    '; ---------------------------------------------------------------------------',
  );
  assert.doesNotMatch(noDragRelease, /GeosShellRedraw/);
});

test('Firmware Update opens removable-media browsing without bypassing RunSelected', () => {
  const actionStart = shell.indexOf('GeosActivateFileMenu:');
  const actionEnd = shell.indexOf('GeosFileOpen:', actionStart);
  const firmwareAction = shell.slice(actionStart, actionEnd);

  assert.match(firmwareAction, /lda #rmtSD[\s\S]*jsr GeosShellOpenSource[\s\S]*lda #GeosNoticeFirmware/);
  assert.doesNotMatch(firmwareAction, /DoFlashUpdate|StartSelItem_WaitForTRDots|rCtlStartSelItemWAIT/);
  assert.match(shell, /MsgNoticeFirmware:[^\n]*"OPEN \.HEX; F5 USB; CONFIRM UPDATE Y\/N"/);
});

test('folder views have direct desktop and parent controls, with HOME and STOP back', () => {
  assert.match(shell, /MsgGeosFolder:[^\n]*"\[X\] "/);
  assert.match(shell, /MsgGeosShellFooter2:[^\n]*"\[DESKTOP\].*\[\^ PARENT\].*\[OPEN\]/);
  assert.match(shell, /cmp #ChrHome[\s\S]*?jsr GeosFileDesktop/);
  const back = sourceBlock(shell, 'GeosShellBackOrMenu:', 'GeosShellKeyControl:');
  assert.match(back, /lda GeosSurfaceMode[\s\S]*jsr GeosFileDesktop/);
  const home = sourceBlock(shell, 'GeosFileDesktop:', 'GeosFileParent:');
  assert.match(home, /sta GeosSurfaceMode[\s\S]*sta GeosOverlayMode[\s\S]*sta MouseOpenArmed/);
});

test('drive icons no longer silently alias SD or USB directories', () => {
  const drives = sourceBlock(shell, 'GeosHomeOpenDrive8:', 'GeosHomeOpenGames:');
  assert.doesNotMatch(drives, /rmtSD|rmtUSBDrive|GeosShellOpenSource/);
  assert.match(drives, /lda #8\s+jmp GeosIECOpenDrive/);
  assert.match(drives, /lda #9\s+jmp GeosIECOpenDrive/);
});

test('directory waits keep the bitmap visible while legacy confirmations retain text', () => {
  const wait = sourceBlock(main, 'WaitForTRDots:', 'WaitForTRMain   ;');
  assert.equal((wait.match(/jmp GeosBitmapWait/g) || []).length, 2);
  assert.match(wait, /lda GeosBitmapActive\s+beq \+\s+jmp GeosBitmapWait/);
});
