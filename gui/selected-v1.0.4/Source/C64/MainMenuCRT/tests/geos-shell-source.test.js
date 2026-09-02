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
const preview = fs.readFileSync(path.join(sourceDir, 'DesktopPreview.asm'), 'utf8');
const desktop = fs.readFileSync(path.join(sourceDir, 'GeosDesktop.s'), 'utf8');
const bitmap = fs.readFileSync(path.join(sourceDir, 'GeosBitmap.s'), 'utf8');
const rich = fs.readFileSync(path.join(sourceDir, 'GeosRich.s'), 'utf8');
const iec = fs.readFileSync(path.join(sourceDir, 'GeosIEC.s'), 'utf8');
const items = fs.readFileSync(path.join(repoRoot, 'Source/Teensy/MainMenuItems.h'), 'utf8');
const firmware = fs.readFileSync(path.join(repoRoot, 'Source/Teensy/TeensyROM.h'), 'utf8');

function sourceBlock(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);

  assert.notEqual(start, -1, `missing start marker: ${startMarker}`);
  assert.notEqual(end, -1, `missing end marker: ${endMarker}`);
  return source.slice(start, end);
}

test('native mouse targets exclude empty cell space and use rendered icon coordinates', () => {
  assert.match(shell, /GeosHomeHitTestXYIcon:[\s\S]*?jmp GeosRichHitHome/);
  assert.match(mouse, /MouseHitDesktop:\s*!ifdef DesktopShell \{\s+jsr GeosRichHitFile/);
  assert.match(iec, /GeosIECMouseClick:[\s\S]*?jsr GeosRichHitFile\s+bcc GeosIECMouseNoTarget/);
  const homeHit = sourceBlock(rich, 'GeosRichHitHome:', '; Resolve the existing page slot');
  for (const table of ['TblGeosHomeIconSlot', 'RichSlotX', 'RichSlotXHi', 'RichSlotY', 'RichLabelLo', 'RichLabelHi']) {
    assert.ok(homeHit.includes(table), `home target must follow ${table}`);
  }
  assert.match(homeHit, /adc #19/);
  assert.match(homeHit, /RichHitHomeLabel:\s+jsr RichHitCountLine/);
  assert.match(homeHit, /cmp #GeosHomeIconCount/);
  const fileHit = sourceBlock(rich, 'GeosRichHitFile:', 'RichHitItem:');
  assert.match(fileHit, /jsr GeosHitTest\s+bcs \+\s+rts/);
  assert.match(fileHit, /lda #24\s+sta RichW\s+lda #16\s+sta RichH/);
  assert.match(fileHit, /lda #10\s+sta RichHitLimit\s+lda #2\s+sta RichHitLines/);
  assert.match(fileHit, /lda RichLength\s+beq RichHitFileMiss/);
});

test('mouse drag snap cells use the same 60x54 pitch as the native home artwork', () => {
  const snap = sourceBlock(shell, 'GeosHomeHitTestXYSlot:', 'GeosHomeHitTestXYIcon:');
  assert.match(snap, /lda MouseFrameX\s+cmp #150/);
  assert.match(snap, /sbc #30/);
  assert.match(snap, /lda MouseFrameY\s+cmp #20/);
  assert.match(snap, /cmp #74/);
  assert.match(snap, /cmp #128/);
});

test('vertical desktop navigation skips empty rows instead of losing shifted Up', () => {
  for (const direction of ['Up', 'Down']) {
    const block = sourceBlock(shell, `GeosHomeMove${direction}:`,
      direction === 'Up' ? 'GeosHomeMoveDown:' : 'GeosHomeMoveFound:');
    assert.match(block, /lda #3/);
    assert.match(block, /sta GeosWorkCount/);
    assert.match(block, /jsr GeosHomeSlotToIcon\s+bcs GeosHomeMoveFound\s+dec GeosWorkCount/);
    assert.match(block, new RegExp(`jmp GeosHome${direction}Loop`));
  }
});

test('the compact cartridge boots the flash-backed standalone desktop', () => {
  assert.match(payload, /!set DesktopShell=1/);
  assert.match(wrapper, /\* = \$0801[\s\S]*!binary "build\/DesktopShellCode\.bin"/);
  assert.match(main, /!ifndef DesktopShell[\s\S]*ldx #9[\s\S]*lda #3[\s\S]*jmp DirectRunFromTeensyMenu/);
  assert.match(main, /DirectRunFromTeensyMenu:[\s\S]*lda #GeosSurfaceBrowser[\s\S]*sta GeosOverlayMode[\s\S]*jsr SelectItem[\s\S]*jsr SelectItem/);
  assert.match(items, /\/\*3\*\/rtFilePrg[\s\S]*"TeensyROM Desktop Shell"[\s\S]*DesktopShell_prg/);
  assert.match(firmware, /#include "TRMenuFiles\/ROMs\/DesktopShell\.prg\.h"/);
});

test('desktop exposes five menus, drives and control panel with deletion in the File menu', () => {
  assert.match(shell, /"TR DESK FILE EDIT VIEW DISK {13}"/);
  assert.match(shell, /TblGeosMenuCount:\s*!byte 7,6,3,4,5/);
  assert.match(shell, /GeosMouseOpenDesk:\s+lda #GeosMenuDesk\s+jmp GeosMouseOpenMenu/);
  assert.match(rich, /RichDrive8:[^\n]*"DRIVE 8"/);
  assert.match(rich, /RichDrive9:[^\n]*"DRIVE 9"/);
  assert.match(rich, /RichGames:[^\n]*"GAMES"/);
  assert.match(rich, /RichUtilities:[^\n]*"UTILITIES"/);
  assert.match(rich, /RichControl:[^\n]*"CONTROL",13,"PANEL"/);
  assert.doesNotMatch(shell, /MsgHomeTrash:|GeosHomeOpenTrash:/);
  assert.match(shell, /GeosHomeIconCount = 8/);
  assert.match(shell, /GeosActivateFileMenu:[\s\S]*?cmp #4\s+bne \+\s+jmp GeosFileDelete/);
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

test('expanded browser page buttons flank a non-clickable page count', () => {
  const browserHeader = sourceBlock(
    shell,
    'GeosShellDrawBrowserHeader:',
    'GeosShellDrawBrowserFooter:',
  );
  const pageHit = sourceBlock(shell, 'GeosMouseBrowserPage:', 'GeosMouseBrowserToolbar:');
  assert.match(browserHeader, /ldx #1\s+ldy #27[\s\S]*lda #<MsgGeosPage/);
  assert.match(
    pageHit,
    /cpx #25[\s\S]*cpx #27[\s\S]*MouseEventPagePrev[\s\S]*cpx #38[\s\S]*cpx #40[\s\S]*MouseEventPageNext/,
  );
});

test('clock-adjacent media control uses the established F4 SID toggle', () => {
  const menuHit = sourceBlock(shell, 'GeosMouseMenuBar:', 'GeosMouseDropdown:');
  assert.match(
    menuHit,
    /cpx #22\s+bcc GeosMouseOpenDisk\s+cpx #28\s+bcc GeosMouseDismissMenu\s+cpx #30\s+bcc GeosMouseToggleSID/,
  );
  assert.match(
    menuHit,
    /GeosMouseToggleSID:\s+lda GeosOverlayMode\s+cmp #GeosOverlayMenu\s+bne \+\s+jmp GeosMouseCloseOverlay\s+\+\s+lda #ChrF4\s+jmp MouseReturnVirtualKey/,
  );
  assert.match(main, /cmp #ChrF4[\s\S]*jsr ToggleSIDMusic/);
});

test('clicking the same open header closes it while another header switches menus', () => {
  const toggle = sourceBlock(shell, 'GeosShellToggleMenu:', 'GeosShellOpenControl:');
  assert.match(toggle, /tax\s+lda GeosOverlayMode\s+cmp #GeosOverlayMenu\s+bne GeosShellToggleMenuOpen\s+cpx GeosActiveMenu\s+bne GeosShellToggleMenuOpen\s+jmp GeosMouseCloseOverlay/);
  assert.match(toggle, /GeosShellToggleMenuOpen:\s+txa\s+jmp GeosShellOpenMenu/);
  assert.match(shell, /GeosMouseOpenMenu:\s+jsr GeosShellToggleMenu\s+jmp MouseNoTarget/);
});

test('clicking outside a menu dismisses it without activating the underlying surface', () => {
  const bar = sourceBlock(shell, 'GeosMouseMenuBar:', 'GeosMouseOpenDesk:');
  assert.match(bar, /cpx #6\s+bcc GeosMouseOpenDesk\s+cpx #10\s+bcc GeosMouseOpenFile\s+cpx #14\s+bcc GeosMouseOpenEdit\s+cpx #18\s+bcc GeosMouseOpenView\s+cpx #22\s+bcc GeosMouseOpenDisk/);
  assert.match(bar, /cpx #28\s+bcc GeosMouseDismissMenu/);
  assert.match(bar, /cpx #30\s+bcc GeosMouseToggleSID\s+jmp GeosMouseDismissMenu/);
  assert.match(shell, /GeosMouseDismissMenu:\s+lda GeosOverlayMode\s+cmp #GeosOverlayMenu\s+bne \+\s+jmp GeosMouseCloseOverlay\s+\+\s+jmp MouseNoTarget/);
  assert.match(shell, /GeosMouseDropdown:\s+ldx MouseFrameX\s+ldy MouseFrameY\s+jsr GeosShellMenuHitTest\s+bcs \+\s+jmp GeosMouseCloseOverlay/);
  assert.match(shell, /GeosMouseCloseOverlay:\s+lda #GeosOverlayNone\s+sta GeosOverlayMode\s+sta GeosNotice\s+jsr GeosShellRedraw\s+jmp MouseNoTarget/);
  assert.match(mouse, /MouseNoTarget:\s+lda #0\s+sta MouseOpenArmed\s+clc\s+rts/);
});

test('dropdown bounds exclude the right edge and the row below the last item', () => {
  const hit = sourceBlock(shell, 'GeosShellMenuHitTest:', 'GeosMouseControl:');
  assert.match(hit, /stx GeosWorkCol\s+cpy #10\s+bcc GeosShellMenuMiss/);
  assert.match(hit, /sbc #10\s+ldx #0\s+GeosMenuHitRow:\s+cmp #12\s+bcc \+\s+sbc #12\s+inx\s+bne GeosMenuHitRow\s+\+\s+stx GeosWorkItem\s+txa\s+ldx GeosActiveMenu\s+cmp TblGeosMenuCount,x\s+bcs GeosShellMenuMiss/);
  assert.match(hit, /cmp RichDropdownHalfX,x\s+bcc GeosShellMenuMiss\s+sec\s+sbc RichDropdownHalfX,x\s+cmp RichDropdownHalfWidth,x\s+bcs GeosShellMenuMiss/);
  assert.match(hit, /RichDropdownHalfX:\s*!byte 0,24,40,56,72/);
  assert.match(hit, /RichDropdownHalfWidth:\s*!byte 60,64,64,56,68/);
  assert.match(hit, /lda GeosWorkItem\s+sec\s+rts\s+GeosShellMenuMiss:\s+clc\s+rts/);
  assert.doesNotMatch(hit, /GeosShellMenuActivate|MouseFrameX/);
});

test('VICE preview shares menu toggles and outside dismissal without invoking hardware actions', () => {
  const click = sourceBlock(preview, 'PreviewMouseClick:', '; Snapshots after first home');
  assert.match(click, /cpx #22\s+bcs PreviewMouseDismissMenu\s+jmp GeosMouseMenuBar/);
  assert.match(click, /PreviewMouseDismissMenu:\s+jmp GeosMouseDismissMenu/);
  assert.match(click, /cmp #GeosOverlayMenu\s+bne PreviewMouseDone\s+ldx MouseFrameX\s+ldy MouseFrameY\s+jsr GeosShellMenuHitTest\s+bcc \+\s+sta GeosMenuSelection\s+jsr PreviewActivateApp/);
  assert.match(click, /PreviewActivateApp:\s+lda GeosOverlayMode\s+cmp #GeosOverlayMenu\s+bne PreviewMouseDone\s+lda GeosActiveMenu\s+bne PreviewMouseDone\s+lda GeosMenuSelection\s+cmp #4\s+bcc PreviewMouseDone\s+jmp GeosShellMenuActivate/);
  assert.doesNotMatch(click, /GeosMouseToggleSID|GeosShellOpenMenu|GeosShellOpenSource/);
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

test('home status is bitmap-native and cannot scroll away the menu bar', () => {
  const home = sourceBlock(rich, 'GeosRichHome:', 'RichHomeIcon:');
  assert.match(home, /lda #189\s+sta RichY/);
  assert.match(home, /lda TblGeosHomeStatus,x\s+ldy TblGeosHomeStatus\+1,x\s+jsr RichText/);
  assert.doesNotMatch(home, /GeosBlankLine|SetCursor|SendChar/);
  assert.doesNotMatch(shell, /GeosShellDrawHomeStatus:|GeosShellDrawLegacyHome:/);
});

test('home desktop uses native icon assets without emitting obsolete character icons', () => {
  const home = sourceBlock(rich, 'GeosRichHome:', 'RichHomeIcon:');
  const icon = sourceBlock(rich, 'RichHomeIcon:', 'GeosRichFileNames:');
  assert.match(home, /RichHomeIconLoop:\s+jsr RichHomeIcon/);
  assert.match(icon, /lda RichIconLo,x\s+sta RichSource\+1\s+lda RichIconHi,x\s+sta RichSource\+2/);
  assert.match(icon, /jsr RichBlit/);
  assert.doesNotMatch(shell, /GeosHomeIconData:|GeosDrawHomeIcon:/);
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
  assert.match(shell, /MsgGeosFolder:[^\n]*"    "/);
  assert.match(shell, /MsgGeosUpButton:[^\n]*"     "/);
  assert.match(rich, /RichBrowserClose:[\s\S]*RichBrowserUp:/);
  assert.match(rich, /RichBrowserGadgetX:\s*!byte 0,0,200,48/);
  assert.match(rich, /RichBrowserGadgetY:\s*!byte 8,16,8,8/);
  for (const source of [shell, iec]) {
    assert.match(source, /ldx #2\s+ldy #0\s+clc\s+jsr SetCursor\s+lda #<MsgGeosUpButton/);
  }
  const toolbar = sourceBlock(shell, 'GeosMouseBrowserToolbar:', 'GeosMouseFunctionBar:');
  assert.match(toolbar, /cpy #2\s+bne GeosMouseBrowserSources\s+cpx #4\s+bcs GeosMouseBrowserNoTarget\s+jmp MouseReturnParent/);
  assert.match(shell, /cpy #1\s+bne GeosMouseBrowserToolbar\s+cpx #3\s+bcs GeosMouseBrowserPage\s+jsr GeosFileDesktop/);
  assert.match(iec, /cpy #1\s+bne \+\s+cpx #3\s+bcs GeosIECMousePage\s+jsr GeosFileDesktop/);
  assert.match(shell, /cmp #ChrHome[\s\S]*?jsr GeosFileDesktop/);
  const back = sourceBlock(shell, 'GeosShellBackOrMenu:', 'GeosShellKeyControl:');
  assert.match(back, /lda GeosSurfaceMode[\s\S]*jsr GeosFileDesktop/);
  const home = sourceBlock(shell, 'GeosFileDesktop:', 'GeosFileParent:');
  assert.match(home, /sta GeosSurfaceMode[\s\S]*sta GeosOverlayMode[\s\S]*sta MouseOpenArmed/);
});

test('browser footer is one bitmap-native F-key strip without scrolling the layout', () => {
  const footer = sourceBlock(shell, 'GeosShellDrawBrowserFooter:', 'GeosShellDrawOverlay:');
  assert.doesNotMatch(footer, /MsgGeosShellFooter[123]|MouseHitSourceBar|MouseHitActionBar/);
  assert.doesNotMatch(footer, /ldx #24\s+jsr GeosBlankLine/);
  assert.doesNotMatch(shell, /ldx #24\s+jsr GeosBlankLine/);
  assert.match(rich, /RichComposeFiles:\s+jsr GeosRichFileNames\s+jsr GeosRichBrowserFooter/);
  const nativeFooter = sourceBlock(rich, 'GeosRichBrowserFooter:', '; Eight-pixel top bar');
  assert.match(nativeFooter, /lda #192\s+sta RichY/);
  assert.match(nativeFooter, /RichFunctionHitLeft:\s*!byte 2,38,62,92,122/);
  assert.match(nativeFooter, /RichFunctionHitRight:\s*!byte 23,53,80,119,146/);
  assert.match(nativeFooter, /RichF1: !text "F1 HELP"/);
  assert.match(nativeFooter, /RichF7: !text "F7 TEENSY"/);
  assert.match(nativeFooter, /RichFunctionKey:\s*!byte ChrF1,ChrF3,ChrF5,ChrF7,ChrF8/);
  assert.doesNotMatch(nativeFooter, /HOME|PARENT|DESKTOP|\[OPEN\]|PAGE|ITEM/);
  for (const key of [1, 3, 5, 7, 8]) {
    assert.equal((nativeFooter.match(new RegExp(`RichF${key}:`, 'g')) || []).length, 1);
  }
});

test('browser footer gaps and removed toolbar rows cannot activate hidden controls', () => {
  const toolbar = sourceBlock(shell, 'GeosMouseBrowserToolbar:', 'GeosMouseMenuBar:');
  assert.match(toolbar, /cpy #24\s+bne \+\s+jmp GeosMouseFunctionBar/);
  assert.match(toolbar, /cpy #3\s+bcc GeosMouseBrowserNoTarget\s+cpy #GeosGridTop\+GeosGridRows\*GeosCellHeight\s+bcs GeosMouseBrowserNoTarget\s+jmp MouseHitDesktop/);
  assert.doesNotMatch(toolbar, /MouseHitSourceBar|MouseHitActionBar|GeosMouseBrowserOpen|GeosFileDesktop|ChrReturn|cpy #2[0-3]/);
  assert.match(toolbar, /lda MouseFrameX\s+cmp RichFunctionHitLeft,x\s+bcc \+\s+cmp RichFunctionHitRight,x\s+bcs \+\s+lda RichFunctionKey,x\s+jmp MouseReturnVirtualKey/);
  assert.match(toolbar, /cpx #5\s+bne -\s+jmp MouseNoTarget/);
  assert.match(iec, /GeosIECMousePage:\s+jmp GeosMouseBrowserPage\s+\+\s+jmp GeosMouseBrowserToolbar/);
});

test('browser status preserves selection without painting over the fifth icon row', () => {
  const liveStatus = sourceBlock(bitmap, 'GeosBitmapDrawBrowserStatus:', 'GeosBitmapLegacyMetadata:');
  assert.match(liveStatus, /GeosBitmapDrawBrowserStatus:\s+jmp GeosBitmapRefreshBrowserSelection/);
  assert.doesNotMatch(liveStatus, /GeosBitmapBlankLine|rsstItemName|MsgGeosSelected/);
  assert.doesNotMatch(liveStatus, /MsgGeosType|MsgGeosItem|MsgGeosPageStatus|ldx #20/);
  assert.doesNotMatch(bitmap, /(?:jsr|jmp) GeosBitmapLegacyMetadata/);
  const textStatus = sourceBlock(desktop, 'GeosDrawStatus:', 'GeosStatusDone:');
  assert.match(textStatus, /!ifdef DesktopShell \{\s+lda rwRegCursorItemOnPg\+IO1Port\s+cmp rRegNumItemsOnPage\+IO1Port\s+bcs GeosStatusDone\s+sta rwRegSelItemOnPage\+IO1Port\s+\}\s+!ifndef DesktopShell/);
  const iecStatus = sourceBlock(iec, 'GeosIECDrawStatus:', 'GeosIECGetEntry:');
  assert.match(iecStatus, /jsr GeosBitmapConvertScreen[\s\S]*jmp GeosBitmapShowMessage/);
  assert.doesNotMatch(iecStatus, /SetCursor|PrintString|ldx #19/);
  assert.match(iecStatus, /MsgIECError/);
  assert.match(iecStatus, /MsgIECEmpty/);
  assert.doesNotMatch(iecStatus, /MsgIECHelp|MsgIECPage|MsgGeosPageStatus|ldx #20|GeosIECPrintName/);
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
