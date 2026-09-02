; Expanded GEOS-inspired shell.  This module is compiled only into the
; PROGMEM DesktopShell PRG; the resident 8 KiB cartridge remains a compact
; bootstrap/recovery menu.

   GeosSurfaceHome = 0
   GeosSurfaceBrowser = 1
   GeosSurfaceIEC = 2

   GeosOverlayNone = 0
   GeosOverlayMenu = 1
   GeosOverlayControl = 2
   GeosOverlayArrange = 3

   GeosMenuDesk = 0
   GeosMenuFile = 1
   GeosMenuEdit = 2
   GeosMenuView = 3
   GeosMenuDisk = 4
   GeosMenuCount = 5

   GeosHomeIconCount = 8
   GeosHomeSlotCount = 15
   GeosControlItemCount = 8

   GeosNoticeNone = 0
   GeosNoticeAbout = 1
   GeosNoticeFirmware = 2
   GeosNoticeSaved = 3
   GeosNoticeFileScope = 4

   GeosHomeIconFirst = $5c
   GeosHomeIconChip = GeosHomeIconFirst
   GeosHomeIconSD = GeosHomeIconChip+4
   GeosHomeIconUSB = GeosHomeIconSD+4
   GeosHomeIconDrive = GeosHomeIconUSB+4
   GeosHomeIconFolder = GeosHomeIconDrive+4
   GeosHomeIconControl = GeosHomeIconFolder+4

; ---------------------------------------------------------------------------
; Shell state and initialization

GeosShellInit:
   lda #GeosSurfaceHome
   sta GeosSurfaceMode
   lda #0
   sta GeosOverlayMode
   sta GeosActiveMenu
   sta GeosMenuSelection
   sta GeosHomeSelection
   sta GeosControlSelection
   sta GeosNotice
   sta GeosDragActive
   sta GeosMouseWasDown
   lda #$ff
   sta GeosDragCandidate

   ;Load eight icon positions; the old ninth EEPROM slot stays reserved.
   ldx #0
GeosLoadSlots:
   lda rwRegDesktopSlotStart+IO1Port,x
   cmp #GeosHomeSlotCount
   bcs GeosUseDefaultSlots
   sta GeosWorkSlot
   txa
   beq GeosStoreSlot
   tay
GeosCheckDuplicateSlot:
   dey
   lda TblGeosHomeIconSlot,y
   cmp GeosWorkSlot
   beq GeosUseDefaultSlots
   tya
   bne GeosCheckDuplicateSlot
GeosStoreSlot:
   lda GeosWorkSlot
   sta TblGeosHomeIconSlot,x
   inx
   cpx #GeosHomeIconCount
   bne GeosLoadSlots
   rts

GeosUseDefaultSlots:
   ldx #0
GeosDefaultSlotLoop:
   txa
   sta TblGeosHomeIconSlot,x
   inx
   cpx #GeosHomeIconCount
   bne GeosDefaultSlotLoop
   rts

GeosShellEnterBrowser:
   lda #GeosSurfaceBrowser
   sta GeosSurfaceMode
   lda #GeosOverlayNone
   sta GeosOverlayMode
   lda #GeosNoticeNone
   sta GeosNotice
   lda #$ff
   sta GeosDragCandidate
   rts

GeosShellUsesLocalSelection:
   lda GeosViewMode
   beq GeosShellSelectionIsBackend
   lda GeosOverlayMode
   bne GeosShellSelectionIsLocal
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   beq GeosShellSelectionIsLocal
   lda GeosSurfaceMode
   bne GeosShellSelectionIsBackend
GeosShellSelectionIsLocal:
   sec
   rts
GeosShellSelectionIsBackend:
   clc
   rts

GeosShellMouseSelectionValue:
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   lda GeosMenuSelection
   rts
+  cmp #GeosOverlayControl
   bne +
   lda GeosControlSelection
   rts
+  cmp #GeosOverlayArrange
   beq GeosMouseSelectionHome
   lda GeosSurfaceMode
   bne +
GeosMouseSelectionHome:
   lda GeosHomeSelection
   rts
+  cmp #GeosSurfaceIEC
   bne +
   lda GeosIECSelection
   rts
+  lda rwRegCursorItemOnPg+IO1Port
   rts

; ---------------------------------------------------------------------------
; Home desktop and browser chrome

GeosShellDrawHome:
   ;The native compositor draws this surface directly from icon/selection state.
   jmp GeosInstallMonoCharset
GeosShellDrawMenuBar:
   ldx #0
   ldy #0
   clc
   jsr SetCursor
   lda #PokeBlack
   sta $0286
   lda #<MsgGeosShellMenuBar
   ldy #>MsgGeosShellMenuBar
   jsr PrintString
   jsr DisplayTime
   rts

GeosShellDrawBrowserHeader:
   jsr GeosShellDrawMenuBar
   ldx #1
   ldy #0
   clc
   jsr SetCursor
   lda #PokeBlack
   sta $0286
   lda #<MsgGeosFolder
   ldy #>MsgGeosFolder
   jsr PrintString
   lda rWRegCurrMenuWAIT+IO1Port
   asl
   tax
   lda TblMsgMenuName,x
   ldy TblMsgMenuName+1,x
   jsr PrintString

   ldx #1
   ldy #27
   clc
   jsr SetCursor
   lda #<MsgGeosPage
   ldy #>MsgGeosPage
   jsr PrintString
   lda rwRegPageNumber+IO1Port
   jsr PrintIntByte
   lda #'/'
   jsr SendChar
   lda rRegNumPages+IO1Port
   jsr PrintIntByte

   ldx #2
   ldy #0
   clc
   jsr SetCursor
   lda #<MsgGeosUpButton
   ldy #>MsgGeosUpButton
   jsr PrintString
   lda #rsstShortDirPath
   ldx #35
   jsr GeosPrintSerialLimited
   rts

GeosShellDrawBrowserFooter:
   ldx #21
   jsr GeosBlankLine
   lda GeosNotice
   beq +
   ldx #21
   ldy #0
   clc
   jsr SetCursor
   jsr GeosShellPrintNotice
+  ;CHRCLR already cleared the layout. Do not write 40 chars on row 24:
   ;the KERNAL would scroll the title/path out of this off-screen layout.
   ;One bitmap-native F-key strip is drawn after layout. No duplicate toolbar.
   rts

GeosShellPrintNotice:
   lda GeosNotice
   asl
   tax
   lda TblGeosNotice,x
   ldy TblGeosNotice+1,x
   jmp PrintString

; ---------------------------------------------------------------------------
; Menu and Control Panel drawing

GeosShellDrawOverlay:
   ;Pixel-native overlays are composed after the off-screen browser layout.
   rts
GeosShellRedraw:
   jsr ListMenuItems
   rts

; The backend page map omits its synthetic parent only in bitmap/icon view.
; Its raw directory entries remain available to the compact/classic list.
GeosSyncMenuView:
   lda GeosViewMode
   cmp rwRegMenuView+IO1Port
   beq +
   sta rwRegMenuView+IO1Port
   jsr WaitForTRWaitMsg
+  rts

; ---------------------------------------------------------------------------
; Unified keyboard/joystick action routing

GeosShellHandleKey:
   sta GeosShellKey
   lda GeosViewMode
   bne +
   jmp GeosShellKeyNotHandled
+
   lda GeosOverlayMode
   bne GeosShellAfterFileKeys
   lda GeosShellKey
   cmp #'C'
   bne +
   jsr GeosFileCopy
   jmp GeosShellKeyHandled
+  cmp #'P'
   bne +
   jsr GeosFilePaste
   jmp GeosShellKeyHandled
+  cmp #'D'
   bne GeosShellAfterFileKeys
   jsr GeosFileDelete
   jmp GeosShellKeyHandled
GeosShellAfterFileKeys:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   lda GeosShellKey
   jsr GeosIECHandleKey
   bcc +
   jmp GeosShellKeyHandled
+
   lda GeosShellKey
   cmp #MouseEventMenuDesk
   bcc GeosShellCheckStop
   cmp #MouseEventMenuDisk+1
   bcs GeosShellCheckStop
   sec
   sbc #MouseEventMenuDesk
   jsr GeosShellOpenMenu
   jmp GeosShellKeyHandled

GeosShellCheckStop:
   lda GeosShellKey
   cmp #ChrHome
   bne +
   jsr GeosFileDesktop
   jmp GeosShellKeyHandled
+  lda GeosShellKey
   cmp #ChrStop
   beq GeosShellBackOrMenu
   cmp #ChrRun
   beq GeosShellBackOrMenu
   cmp #ChrF8
   beq GeosShellKeyControl
   cmp #ChrUpArrow
   bne GeosShellKeyNotHandled
   lda GeosOverlayMode
   beq GeosShellKeyNotHandled
GeosShellCloseOverlayKey:
   lda GeosOverlayMode
   cmp #GeosOverlayArrange
   bne +
   jsr GeosShellCancelArrange
   jmp GeosShellKeyHandled
+  lda #GeosOverlayNone
   sta GeosOverlayMode
   jsr GeosShellRedraw
   jmp GeosShellKeyHandled

GeosShellBackOrMenu:
   lda GeosOverlayMode
   bne GeosShellCloseOverlayKey
   lda GeosSurfaceMode
   beq +
   jsr GeosFileDesktop
   jmp GeosShellKeyHandled
+
   lda #GeosMenuDesk
   jsr GeosShellOpenMenu
   jmp GeosShellKeyHandled

GeosShellKeyControl:
   jsr GeosShellOpenControl
GeosShellKeyHandled:
   lda GeosShellKey
   sec
   rts
GeosShellKeyNotHandled:
   lda GeosShellKey
   clc
   rts

GeosShellOpenMenu:
   sta GeosActiveMenu
   lda #0
   sta GeosMenuSelection
   sta GeosNotice
   lda #GeosOverlayMenu
   sta GeosOverlayMode
   jsr GeosShellRedraw
   rts

; A=clicked header. Only the currently open menu toggles closed; a different
; header switches directly, including when another kind of panel was open.
GeosShellToggleMenu:
   tax
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne GeosShellToggleMenuOpen
   cpx GeosActiveMenu
   bne GeosShellToggleMenuOpen
   jmp GeosMouseCloseOverlay
GeosShellToggleMenuOpen:
   txa
   jmp GeosShellOpenMenu

GeosShellOpenControl:
   lda #GeosOverlayControl
   sta GeosOverlayMode
   lda #0
   sta GeosControlSelection
   sta GeosNotice
   jsr GeosShellRedraw
   rts

GeosShellCursorUp:
   lda GeosViewMode
   bne +
   jmp GeosShellCursorBackend
+
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMenuItemUp
+
   cmp #GeosOverlayControl
   bne +
   jmp GeosControlItemUp
+
   cmp #GeosOverlayArrange
   bne +
   jmp GeosArrangeMoveUp
+
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jsr GeosIECMoveUp
   sec
   rts
+  lda GeosSurfaceMode
   beq +
   jmp GeosShellCursorBackend
+
   jsr GeosHomeMoveUp
   sec
   rts

GeosShellCursorDown:
   lda GeosViewMode
   bne +
   jmp GeosShellCursorBackend
+
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMenuItemDown
+
   cmp #GeosOverlayControl
   bne +
   jmp GeosControlItemDown
+
   cmp #GeosOverlayArrange
   bne +
   jmp GeosArrangeMoveDown
+
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jsr GeosIECMoveDown
   sec
   rts
+  lda GeosSurfaceMode
   beq +
   jmp GeosShellCursorBackend
+
   jsr GeosHomeMoveDown
   sec
   rts

GeosShellCursorLeft:
   lda GeosViewMode
   bne +
   jmp GeosShellCursorBackend
+
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMenuPrevious
+
   cmp #GeosOverlayControl
   bne +
   jmp GeosControlItemUp
+
   cmp #GeosOverlayArrange
   bne +
   jmp GeosArrangeMoveLeft
+
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jsr GeosIECMoveLeft
   sec
   rts
+  lda GeosSurfaceMode
   beq +
   jmp GeosShellCursorBackend
+
   jsr GeosHomeMoveLeft
   sec
   rts

GeosShellCursorRight:
   lda GeosViewMode
   bne +
   jmp GeosShellCursorBackend
+
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMenuNext
+
   cmp #GeosOverlayControl
   bne +
   jmp GeosControlItemDown
+
   cmp #GeosOverlayArrange
   bne +
   jmp GeosArrangeMoveRight
+
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jsr GeosIECMoveRight
   sec
   rts
+  lda GeosSurfaceMode
   beq +
   jmp GeosShellCursorBackend
+
   jsr GeosHomeMoveRight
   sec
   rts
GeosShellCursorBackend:
   clc
   rts

GeosMenuItemUp:
   lda GeosMenuSelection
   bne +
   ldx GeosActiveMenu
   lda TblGeosMenuCount,x
   sta GeosMenuSelection
+  dec GeosMenuSelection
   jsr GeosShellRedraw
   sec
   rts

GeosMenuItemDown:
   inc GeosMenuSelection
   ldx GeosActiveMenu
   lda GeosMenuSelection
   cmp TblGeosMenuCount,x
   bcc +
   lda #0
   sta GeosMenuSelection
+  jsr GeosShellRedraw
   sec
   rts

GeosMenuPrevious:
   lda GeosActiveMenu
   bne +
   lda #GeosMenuCount
   sta GeosActiveMenu
+  dec GeosActiveMenu
   lda #0
   sta GeosMenuSelection
   jsr GeosShellRedraw
   sec
   rts

GeosMenuNext:
   inc GeosActiveMenu
   lda GeosActiveMenu
   cmp #GeosMenuCount
   bcc +
   lda #0
   sta GeosActiveMenu
+  lda #0
   sta GeosMenuSelection
   jsr GeosShellRedraw
   sec
   rts

GeosControlItemUp:
   lda GeosControlSelection
   bne +
   lda #GeosControlItemCount
   sta GeosControlSelection
+  dec GeosControlSelection
   jsr GeosShellRedraw
   sec
   rts

GeosControlItemDown:
   inc GeosControlSelection
   lda GeosControlSelection
   cmp #GeosControlItemCount
   bcc +
   lda #0
   sta GeosControlSelection
+  jsr GeosShellRedraw
   sec
   rts

; ---------------------------------------------------------------------------
; Home selection, opening, and snap-grid arrangement

GeosHomeMoveRight:
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   sta GeosWorkSlot
   lda #GeosHomeSlotCount
   sta GeosWorkCount
GeosHomeRightLoop:
   inc GeosWorkSlot
   lda GeosWorkSlot
   cmp #GeosHomeSlotCount
   bcc +
   lda #0
   sta GeosWorkSlot
+  lda GeosWorkSlot
   jsr GeosHomeSlotToIcon
   bcs GeosHomeMoveFound
   dec GeosWorkCount
   bne GeosHomeRightLoop
   rts

GeosHomeMoveLeft:
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   sta GeosWorkSlot
   lda #GeosHomeSlotCount
   sta GeosWorkCount
GeosHomeLeftLoop:
   lda GeosWorkSlot
   bne +
   lda #GeosHomeSlotCount
   sta GeosWorkSlot
+  dec GeosWorkSlot
   lda GeosWorkSlot
   jsr GeosHomeSlotToIcon
   bcs GeosHomeMoveFound
   dec GeosWorkCount
   bne GeosHomeLeftLoop
   rts

GeosHomeMoveUp:
   ldx GeosHomeSelection
   lda #3                    ;skip empty snap rows while keeping the column
   sta GeosWorkCount
   lda TblGeosHomeIconSlot,x
GeosHomeUpLoop:
   tax
   lda TblGeosSlotUp,x
   sta GeosWorkSlot
   jsr GeosHomeSlotToIcon
   bcs GeosHomeMoveFound
   dec GeosWorkCount
   beq GeosHomeVerticalDone
   lda GeosWorkSlot
   jmp GeosHomeUpLoop
GeosHomeVerticalDone:
   rts

GeosHomeMoveDown:
   ldx GeosHomeSelection
   lda #3
   sta GeosWorkCount
   lda TblGeosHomeIconSlot,x
GeosHomeDownLoop:
   tax
   lda TblGeosSlotDown,x
   sta GeosWorkSlot
   jsr GeosHomeSlotToIcon
   bcs GeosHomeMoveFound
   dec GeosWorkCount
   beq GeosHomeVerticalDone
   lda GeosWorkSlot
   jmp GeosHomeDownLoop

GeosHomeMoveFound:
   sta GeosHomeSelection
   lda #GeosNoticeNone
   sta GeosNotice
   jsr GeosShellRedraw
   rts

; A=slot. Returns C set/A=icon id when occupied.
GeosHomeSlotToIcon:
   sta GeosWorkSlot
   ldx #0
GeosSlotToIconLoop:
   lda TblGeosHomeIconSlot,x
   cmp GeosWorkSlot
   beq +
   inx
   cpx #GeosHomeIconCount
   bne GeosSlotToIconLoop
   clc
   rts
+  txa
   sec
   rts

; A=slot. Returns C set when no icon occupies it.
GeosHomeSlotIsEmpty:
   jsr GeosHomeSlotToIcon
   bcc +
   clc
   rts
+  sec
   rts

GeosShellSelectItem:
   lda GeosViewMode
   beq GeosSelectBackend
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   beq GeosSelectMenu
   cmp #GeosOverlayControl
   beq GeosSelectControl
   cmp #GeosOverlayArrange
   beq GeosSelectArrange
   lda GeosSurfaceMode
   bne GeosSelectBackend
   jsr GeosShellActivateHome
   sec
   rts
GeosSelectMenu:
   jsr GeosShellMenuActivate
   sec
   rts
GeosSelectControl:
   jsr GeosShellLaunchControlPage
   sec
   rts
GeosSelectArrange:
   jsr GeosShellCommitArrange
   sec
   rts
GeosSelectBackend:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jsr GeosIECActivate
   sec
   rts
+
   clc
   rts

GeosShellActivateHome:
   lda GeosHomeSelection
   beq GeosHomeOpenTeensy
   cmp #1
   beq GeosHomeOpenSD
   cmp #2
   beq GeosHomeOpenUSB
   cmp #3
   beq GeosHomeOpenDrive8
   cmp #4
   beq GeosHomeOpenDrive9
   cmp #5
   beq GeosHomeOpenGames
   cmp #6
   beq GeosHomeOpenUtilities
   jmp GeosHomeOpenControl

GeosHomeOpenTeensy:
   lda #rmtTeensy
   jmp GeosShellOpenSource
GeosHomeOpenSD:
   lda #rmtSD
   jmp GeosShellOpenSource
GeosHomeOpenUSB:
   lda #rmtUSBDrive
   jmp GeosShellOpenSource
GeosHomeOpenDrive8:
   lda #8
   jmp GeosIECOpenDrive
GeosHomeOpenDrive9:
   lda #9
   jmp GeosIECOpenDrive
GeosHomeOpenGames:
   lda #0
   jmp GeosShellOpenTeensyFolder
GeosHomeOpenUtilities:
   lda #7
GeosShellOpenTeensyFolder:
   sta GeosFolderIndex
   lda #rmtTeensy
   jsr ListMenuItemsChangeInit
   lda GeosFolderIndex
   sta rwRegCursorItemOnPg+IO1Port
   jsr SelectItem
   rts
GeosHomeOpenControl:
   jmp GeosShellOpenControl

GeosShellOpenSource:
   jsr ListMenuItemsChangeInit
   rts

GeosShellSetNotice:
   sta GeosNotice
   jsr GeosShellRedraw
   rts

GeosShellEnterArrange:
   lda #GeosSurfaceHome
   sta GeosSurfaceMode
   lda #GeosOverlayArrange
   sta GeosOverlayMode
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   sta GeosArrangeOrigin
   jsr GeosShellRedraw
   rts

GeosArrangeMoveLeft:
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   tax
   lda TblGeosSlotLeft,x
   jmp GeosArrangeTrySlot
GeosArrangeMoveRight:
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   tax
   lda TblGeosSlotRight,x
   jmp GeosArrangeTrySlot
GeosArrangeMoveUp:
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   tax
   lda TblGeosSlotUp,x
   jmp GeosArrangeTrySlot
GeosArrangeMoveDown:
   ldx GeosHomeSelection
   lda TblGeosHomeIconSlot,x
   tax
   lda TblGeosSlotDown,x

GeosArrangeTrySlot:
   sta GeosWorkSlot
   jsr GeosHomeSlotIsEmpty
   bcc GeosArrangeMoveDone
   ldx GeosHomeSelection
   lda GeosWorkSlot
   sta TblGeosHomeIconSlot,x
   jsr GeosShellRedraw
GeosArrangeMoveDone:
   sec
   rts

GeosShellCommitArrange:
   ldx GeosHomeSelection
   jsr GeosShellPersistIcon
   lda #GeosOverlayNone
   sta GeosOverlayMode
   lda #GeosNoticeSaved
   sta GeosNotice
   jsr GeosShellRedraw
   rts

GeosShellCancelArrange:
   ldx GeosHomeSelection
   lda GeosArrangeOrigin
   sta TblGeosHomeIconSlot,x
   lda #GeosOverlayNone
   sta GeosOverlayMode
   jsr GeosShellRedraw
   rts

; X=icon id.  One snapped icon move is one existing WAIT-style EEPROM write.
GeosShellPersistIcon:
   lda TblGeosHomeIconSlot,x
   sta rwRegDesktopSlotStart+IO1Port,x
   jsr WaitForTRWaitMsg
   rts

; ---------------------------------------------------------------------------
; Menu activation and Settings-page routing

GeosShellMenuActivate:
   lda #GeosOverlayNone
   sta GeosOverlayMode
   lda GeosActiveMenu
   beq GeosActivateDeskMenu
   cmp #GeosMenuFile
   beq GeosActivateFileMenu
   cmp #GeosMenuEdit
   bne +
   jmp GeosActivateEditMenu
+
   cmp #GeosMenuView
   bne +
   jmp GeosActivateViewMenu
+
   jmp GeosActivateDiskMenu

GeosActivateDeskMenu:
   lda GeosMenuSelection
   cmp #4
   bcc +
   sec
   sbc #4
   jmp GeosShellOpenApp
+  lda GeosMenuSelection
   beq GeosDeskAbout
   cmp #1
   bne +
   jmp GeosHomeOpenControl
+
   cmp #2
   bne +
   jmp GeosMenuRefresh
+
   lda #GeosSurfaceBrowser
   sta GeosSurfaceMode
   lda #0
   sta GeosViewMode
   jmp GeosShellRedraw
GeosDeskAbout:
   lda #GeosNoticeAbout
   jmp GeosShellSetNotice

GeosShellOpenApp:
   jsr GeosAppEntry
   cmp #2
   bne +
   lda #rmtSD
   jmp GeosShellOpenSource
+  jmp GeosShellRedraw

GeosActivateFileMenu:
   lda GeosMenuSelection
   cmp #4
   bne +
   jmp GeosFileDelete
+  lda GeosMenuSelection
   beq GeosFileOpen
   cmp #1
   beq GeosFileDesktop
   cmp #2
   bne +
   jmp GeosFileParent
+
   lda #rmtSD
   jsr GeosShellOpenSource
   lda #GeosNoticeFirmware
   jmp GeosShellSetNotice
GeosFileOpen:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jmp GeosIECActivate
+  lda GeosSurfaceMode
   bne +
   jmp GeosShellActivateHome
+  jsr SelectItem
   rts
GeosFileDesktop:
   lda #GeosSurfaceHome
   sta GeosSurfaceMode
   lda #0
   sta GeosOverlayMode
   sta GeosNotice
   sta MouseOpenArmed
   lda #$ff
   sta GeosDragCandidate
   jmp GeosShellRedraw
GeosFileParent:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jmp GeosIECParent
+  lda GeosSurfaceMode
   beq GeosMenuRefresh
   lda #rCtlUpDirectoryWAIT
   sta wRegControl+IO1Port
   jsr WaitForTRWaitMsg
   jmp GeosShellRedraw

GeosActivateEditMenu:
   lda GeosMenuSelection
   beq GeosEditCopy
   cmp #1
   beq GeosEditPaste
   jmp GeosShellEnterArrange
GeosEditCopy:
   jmp GeosFileCopy
GeosEditPaste:
   jmp GeosFilePaste

GeosActivateViewMenu:
   lda GeosMenuSelection
   beq GeosFileDesktop
   cmp #1
   beq GeosViewIcons
   cmp #2
   beq GeosViewList
GeosMenuRefresh:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jmp GeosIECRefresh
+
   jmp GeosShellRedraw
GeosViewIcons:
   lda #1
   sta GeosViewMode
   jmp GeosShellRedraw
GeosViewList:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   jmp GeosShellRedraw
+
   lda #GeosSurfaceBrowser
   sta GeosSurfaceMode
   lda #0
   sta GeosViewMode
   jmp GeosShellRedraw

GeosActivateDiskMenu:
   lda GeosMenuSelection
   beq GeosDiskTeensy
   cmp #1
   beq GeosDiskSD
   cmp #2
   beq GeosDiskUSB
   cmp #3
   bne +
   jmp GeosHomeOpenDrive8
+
   jmp GeosHomeOpenDrive9
GeosDiskTeensy:
   lda #rmtTeensy
   jmp GeosShellOpenSource
GeosDiskSD:
   lda #rmtSD
   jmp GeosShellOpenSource
GeosDiskUSB:
   lda #rmtUSBDrive
   jmp GeosShellOpenSource

GeosShellLaunchControlPage:
   ldx GeosControlSelection
   lda TblGeosControlPage,x
   ora #$80
   sta rwRegScratch+IO1Port
   ldx #9
   lda #1
   jmp DirectRunFromTeensyMenu

; ---------------------------------------------------------------------------
; Expanded mouse hit testing and drag/drop

; Entered from Mouse1351ProcessMenu with X=character column, Y=character row.
; This routine tail-jumps to the established virtual-key/browser returns.
GeosShellMouseClick:
   cpy #0
   bne +
   jmp GeosMouseMenuBar
+
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMouseDropdown
+
   cmp #GeosOverlayControl
   bne +
   jmp GeosMouseControl
+
   cmp #GeosOverlayArrange
   bne +
   jmp GeosMouseArrange
+
   lda GeosSurfaceMode
   bne +
   jmp GeosMouseHome
+
   cmp #GeosSurfaceIEC
   bne +
   jmp GeosIECMouseClick
+
   cpy #1
   bne GeosMouseBrowserToolbar
   cpx #3
   bcs GeosMouseBrowserPage
   jsr GeosFileDesktop
   jmp MouseNoTarget
GeosMouseBrowserPage:
   cpx #25
   bcc GeosMouseBrowserNoTarget
   cpx #27
   bcs +
   lda #MouseEventPagePrev
   bne GeosMouseBrowserPageKey
+  cpx #38
   bcc GeosMouseBrowserNoTarget
   cpx #40
   bcs GeosMouseBrowserNoTarget
   lda #MouseEventPageNext
GeosMouseBrowserPageKey:
   jmp MouseReturnVirtualKey
GeosMouseBrowserToolbar:
   cpy #2
   bne GeosMouseBrowserSources
   cpx #4
   bcs GeosMouseBrowserNoTarget
   jmp MouseReturnParent
GeosMouseBrowserSources:
   cpy #24
   bne +
   jmp GeosMouseFunctionBar
+  cpy #3
   bcc GeosMouseBrowserNoTarget
   cpy #19
   bcs GeosMouseBrowserNoTarget
   jmp MouseHitDesktop
GeosMouseBrowserNoTarget:
   jmp MouseNoTarget

; Match only the visible labels on the single bottom F-key strip.
GeosMouseFunctionBar:
   ldx #0
-  lda MouseFrameX
   cmp RichFunctionHitLeft,x
   bcc +
   cmp RichFunctionHitRight,x
   bcs +
   lda RichFunctionKey,x
   jmp MouseReturnVirtualKey
+  inx
   cpx #5
   bne -
   jmp MouseNoTarget

GeosMouseMenuBar:
   cpx #6
   bcc GeosMouseOpenDesk
   cpx #10
   bcc GeosMouseOpenFile
   cpx #14
   bcc GeosMouseOpenEdit
   cpx #18
   bcc GeosMouseOpenView
   cpx #22
   bcc GeosMouseOpenDisk
   cpx #28
   bcc GeosMouseDismissMenu
   cpx #30
   bcc GeosMouseToggleSID
   jmp GeosMouseDismissMenu
GeosMouseOpenDesk:
   lda #GeosMenuDesk
   jmp GeosMouseOpenMenu
GeosMouseOpenFile:
   lda #GeosMenuFile
   bne GeosMouseOpenMenu
GeosMouseOpenEdit:
   lda #GeosMenuEdit
   bne GeosMouseOpenMenu
GeosMouseOpenView:
   lda #GeosMenuView
   bne GeosMouseOpenMenu
GeosMouseOpenDisk:
   lda #GeosMenuDisk
   bne GeosMouseOpenMenu
GeosMouseToggleSID:
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMouseCloseOverlay
+
   lda #ChrF4
   jmp MouseReturnVirtualKey
GeosMouseOpenMenu:
   jsr GeosShellToggleMenu
   jmp MouseNoTarget
GeosMouseDismissMenu:
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jmp GeosMouseCloseOverlay
+
   jmp MouseNoTarget

GeosMouseDropdown:
   ldx MouseFrameX
   ldy MouseFrameY
   jsr GeosShellMenuHitTest
   bcs +
   jmp GeosMouseCloseOverlay
+
   sta GeosMenuSelection
   jsr GeosShellMenuActivate
   jmp MouseNoTarget

; X=mouse logical half-pixel, Y=pixel row. C set and A=item inside the panel;
; C clear outside. Shared with the hardware-free preview (no activation here).
GeosShellMenuHitTest:
   stx GeosWorkCol
   cpy #10
   bcc GeosShellMenuMiss
   tya
   sec
   sbc #10
   ldx #0
GeosMenuHitRow:
   cmp #12
   bcc +
   sbc #12
   inx
   bne GeosMenuHitRow
+  stx GeosWorkItem
   txa
   ldx GeosActiveMenu
   cmp TblGeosMenuCount,x
   bcs GeosShellMenuMiss
   lda GeosWorkCol
   cmp RichDropdownHalfX,x
   bcc GeosShellMenuMiss
   sec
   sbc RichDropdownHalfX,x
   cmp RichDropdownHalfWidth,x
   bcs GeosShellMenuMiss
   lda GeosWorkItem
   sec
   rts
GeosShellMenuMiss:
   clc
   rts

RichDropdownHalfX: !byte 0,24,40,56,72
RichDropdownHalfWidth: !byte 60,64,64,56,68

GeosMouseControl:
   cpy #5
   bcs +
   jmp GeosMouseCloseOverlay
+
   cpy #5+GeosControlItemCount
   bcc +
   jmp GeosMouseCloseOverlay
+
   cpx #3
   bcs +
   jmp GeosMouseCloseOverlay
+
   cpx #32
   bcc +
   jmp GeosMouseCloseOverlay
+
   tya
   sec
   sbc #5
   sta MouseHitItem
   lda MouseOpenArmed
   beq GeosMouseSelectControl
   lda MouseLastClickedItem
   cmp MouseHitItem
   bne GeosMouseSelectControl
   lda #0
   sta MouseOpenArmed
   lda #ChrReturn
   sec
   rts
GeosMouseSelectControl:
   lda MouseHitItem
   sta GeosControlSelection
   sta MouseLastClickedItem
   lda #1
   sta MouseOpenArmed
   jsr GeosShellRedraw
   clc
   rts

GeosMouseArrange:
   jsr GeosHomeHitTestXYSlot
   bcs +
   jmp MouseNoTarget
+
   jsr GeosArrangeTrySlot
   lda #ChrReturn
   sec
   rts

GeosMouseHome:
   jsr GeosHomeHitTestXYIcon
   bcs +
   jmp MouseNoTarget
+
   sta MouseHitItem
   sta GeosDragCandidate
   tax
   lda TblGeosHomeIconSlot,x
   sta GeosDragOrigin
   sta GeosDragTarget
   lda #0
   sta GeosDragActive
   lda MouseOpenArmed
   beq GeosMouseSelectHome
   lda MouseLastClickedItem
   cmp MouseHitItem
   bne GeosMouseSelectHome
   lda GeosHomeSelection
   cmp MouseHitItem
   bne GeosMouseSelectHome
   lda #0
   sta MouseOpenArmed
   lda #ChrReturn
   sec
   rts
GeosMouseSelectHome:
   lda MouseHitItem
   sta GeosHomeSelection
   sta MouseLastClickedItem
   lda #1
   sta MouseOpenArmed
   lda #GeosNoticeNone
   sta GeosNotice
   jsr GeosShellRedraw
   clc
   rts

GeosMouseCloseOverlay:
   lda #GeosOverlayNone
   sta GeosOverlayMode
   jsr GeosShellRedraw
   jmp MouseNoTarget

; Drag/arrange snap cells follow the native 60x54 pixel icon pitch.
; Use the frame coordinates, not rounded character columns. C set/A=slot.
GeosHomeHitTestXYSlot:
   lda MouseFrameX
   cmp #150
   bcs GeosHomeHitFail
   ldx #0
-  cmp #30
   bcc +
   sec
   sbc #30
   inx
   bne -
+  stx GeosWorkCol
   lda MouseFrameY
   cmp #20
   bcc GeosHomeHitFail
   cmp #176
   bcs GeosHomeHitFail
   cmp #74
   bcc GeosHomeHitRow0
   cmp #128
   bcc GeosHomeHitRow1
   lda #10
   bne GeosHomeHitRowReady
GeosHomeHitRow1:
   lda #5
   bne GeosHomeHitRowReady
GeosHomeHitRow0:
   lda #0
GeosHomeHitRowReady:
   sta GeosWorkSlot
   lda GeosWorkCol
   clc
   adc GeosWorkSlot
   sec
   rts
GeosHomeHitFail:
   clc
   rts

GeosHomeHitTestXYIcon:
   ;The native artwork uses a 60x54 pixel pitch, not the old character grid.
   ;Only the icon and its actual label are clickable; gaps are not targets.
   jmp GeosRichHitHome

; Called every active mouse frame.  Slot changes beyond the original cell are
; the drag threshold; releases persist exactly one icon-position byte.
GeosShellMouseDragFrame:
   lda MouseFrameDown
   beq GeosMouseDragRelease
   lda #1
   sta GeosMouseWasDown
   lda GeosDragCandidate
   cmp #$ff
   beq GeosMouseDragDone
   lda GeosViewMode
   beq GeosMouseDragDone
   lda GeosSurfaceMode
   bne GeosMouseDragDone
   lda GeosOverlayMode
   bne GeosMouseDragDone

   lda MouseFrameX
   lsr
   lsr
   tax
   lda MouseFrameY
   lsr
   lsr
   lsr
   tay
   jsr GeosHomeHitTestXYSlot
   bcc GeosMouseDragDone
   sta GeosWorkSlot
   cmp GeosDragTarget
   beq GeosMouseDragDone
   jsr GeosHomeSlotIsEmpty
   bcc GeosMouseDragDone
   ldx GeosDragCandidate
   lda GeosWorkSlot
   sta TblGeosHomeIconSlot,x
   sta GeosDragTarget
   lda #1
   sta GeosDragActive
   jsr GeosShellRedraw
GeosMouseDragDone:
   rts

GeosMouseDragRelease:
   lda GeosMouseWasDown
   beq GeosMouseDragDone
   lda #0
   sta GeosMouseWasDown
   lda GeosDragCandidate
   cmp #$ff
   beq GeosMouseDragDone
   lda GeosDragActive
   beq GeosMouseReleaseWithoutDrag
   ldx GeosDragCandidate
   jsr GeosShellPersistIcon
   lda #GeosNoticeSaved
   sta GeosNotice
   ;A completed drag must not also count as the first half of a double-click.
   lda #0
   sta MouseOpenArmed
GeosMouseReleaseClearCandidate:
   lda #$ff
   sta GeosDragCandidate
   lda #0
   sta GeosDragActive
   jsr GeosShellRedraw
   rts
GeosMouseReleaseWithoutDrag:
   ;A normal click already redrew when it selected the icon.  Avoid a second,
   ;expensive bitmap conversion on button-up and preserve double-click timing.
   lda #$ff
   sta GeosDragCandidate
   lda #0
   sta GeosDragActive
   rts

; ---------------------------------------------------------------------------
; Layout, strings, and original monochrome home icon artwork

TblGeosHomeIconSlot:
   !byte 0,1,2,3,4,5,6,8,9

TblGeosSlotLeft:
   !byte 4,0,1,2,3, 9,5,6,7,8, 14,10,11,12,13
TblGeosSlotRight:
   !byte 1,2,3,4,0, 6,7,8,9,5, 11,12,13,14,10
TblGeosSlotUp:
   !byte 10,11,12,13,14, 0,1,2,3,4, 5,6,7,8,9
TblGeosSlotDown:
   !byte 5,6,7,8,9, 10,11,12,13,14, 0,1,2,3,4

TblGeosHomeLabel:
   !word MsgHomeTeensy,MsgHomeSD,MsgHomeUSB,MsgHomeDrive8,MsgHomeDrive9
   !word MsgHomeGames,MsgHomeUtilities,MsgHomeControl
TblGeosHomeStatus:
   !word MsgStatusTeensy,MsgStatusSD,MsgStatusUSB,MsgStatusDrive8,MsgStatusDrive9
   !word MsgStatusGames,MsgStatusUtilities,MsgStatusControl

TblGeosNotice:
   !word MsgNoticeNone,MsgNoticeAbout,MsgNoticeFirmware,MsgNoticeSaved,MsgNoticeFileScope

TblGeosMenuCount: !byte 7,5,3,4,5
TblGeosMenuListLo:
   !byte <TblDeskMenu,<TblFileMenu,<TblEditMenu,<TblViewMenu,<TblDiskMenu
TblGeosMenuListHi:
   !byte >TblDeskMenu,>TblFileMenu,>TblEditMenu,>TblViewMenu,>TblDiskMenu

TblDeskMenu: !word MsgMenuAbout,MsgMenuControl,MsgMenuRefresh,MsgMenuClassic
   !word MsgMenuSnake,MsgMenuCalculator,MsgMenuTextViewer
MsgMenuSnake: !tx "SNAKE",0
MsgMenuCalculator: !tx "CALCULATOR",0
MsgMenuTextViewer: !tx "TEXT VIEWER",0
TblFileMenu: !word MsgMenuOpen,MsgMenuDesktop,MsgMenuParent,MsgMenuFirmware,MsgMenuDelete
TblEditMenu: !word MsgMenuCopy,MsgMenuPaste,MsgMenuArrange
TblViewMenu: !word MsgMenuDesktop,MsgMenuIcons,MsgMenuList,MsgMenuRefresh
TblDiskMenu: !word MsgShellMenuTeensy,MsgShellMenuSD,MsgMenuUSB,MsgMenuDrive8,MsgMenuDrive9

TblGeosControlLabel:
   !word MsgControlAppearance,MsgControlInput,MsgControlStartup,MsgControlStorage
   !word MsgControlClock,MsgControlMidiNet,MsgControlSystem,MsgControlAdvanced
TblGeosControlPage:
   !byte 3,1,2,1,5,4,6,0

MsgGeosShellMenuBar:
   !tx ChrRvsOn,"TR DESK FILE EDIT VIEW DISK             ",ChrRvsOff,0
MsgGeosFolder:       !tx "    ",0 ;room for the native close gadget
MsgGeosUpButton:     !tx "     ",0 ;room for the native parent-arrow gadget
MsgHomeTeensy:    !tx " TEENSY ",0
MsgHomeSD:        !tx "SD CARD ",0
MsgHomeUSB:       !tx "  USB   ",0
MsgHomeDrive8:    !tx "DRIVE 8 ",0
MsgHomeDrive9:    !tx "DRIVE 9 ",0
MsgHomeGames:     !tx " GAMES  ",0
MsgHomeUtilities: !tx " UTILS  ",0
MsgHomeControl:   !tx "CONTROL ",0

MsgStatusTeensy:    !tx "TEENSY MEMORY - READY",0
MsgStatusSD:        !tx "SD CARD - OPEN FILES",0
MsgStatusUSB:       !tx "USB STORAGE - OPEN FILES",0
MsgStatusDrive8:    !tx "DRIVE 8 - OPEN DISK DIRECTORY",0
MsgStatusDrive9:    !tx "DRIVE 9 - OPEN DISK DIRECTORY",0
MsgStatusGames:     !tx "GAMES FOLDER",0
MsgStatusUtilities: !tx "UTILITIES FOLDER",0
MsgStatusControl:   !tx "CONTROL PANEL",0

MsgNoticeNone:     !tx "READY",0
MsgNoticeAbout:    !tx "TEENSYROM DESK - CUSTOM GUI",0
MsgNoticeFirmware: !tx "OPEN .HEX; F5 USB; CONFIRM UPDATE Y/N",0
MsgNoticeSaved:    !tx "DESKTOP POSITION SAVED",0
MsgNoticeFileScope:!tx "FILE OPERATIONS NEED SD OR USB FILES",0

MsgMenuAbout:    !tx "ABOUT TEENSYROM",0
MsgMenuControl:  !tx "CONTROL PANEL",0
MsgMenuRefresh:  !tx "REFRESH",0
MsgMenuClassic:  !tx "CLASSIC MENU",0
MsgMenuOpen:     !tx "OPEN",0
MsgMenuDesktop:  !tx "DESKTOP",0
MsgMenuParent:   !tx "PARENT FOLDER",0
MsgMenuFirmware: !tx "FIRMWARE UPDATE",0
MsgMenuCopy:     !tx "COPY     SHIFT+C",0
MsgMenuPaste:    !tx "PASTE    SHIFT+P",0
MsgMenuDelete:   !tx "DELETE... SHIFT+D",0
MsgMenuArrange:  !tx "ARRANGE ICONS",0
MsgMenuIcons:    !tx "ICONS",0
MsgMenuList:     !tx "LIST",0
MsgShellMenuTeensy: !tx "TEENSY MEMORY",0
MsgShellMenuSD:     !tx "SD CARD",0
MsgMenuUSB:      !tx "USB STORAGE",0
MsgMenuDrive8:   !tx "DRIVE 8",0
MsgMenuDrive9:   !tx "DRIVE 9",0

MsgControlAppearance:!tx "APPEARANCE       COLORS",0
MsgControlInput:     !tx "INPUT            GENERAL/HOTKEYS",0
MsgControlStartup:   !tx "STARTUP          BOOT OPTIONS",0
MsgControlStorage:   !tx "STORAGE          KERNAL/REU",0
MsgControlClock:     !tx "CLOCK            TIME/RTC",0
MsgControlMidiNet:   !tx "MIDI/NETWORK     MIDI SETTINGS",0
MsgControlSystem:    !tx "SYSTEM           INFORMATION",0
MsgControlAdvanced:  !tx "ADVANCED...      ALL SETTINGS",0

GeosSurfaceMode:       !byte GeosSurfaceHome
GeosOverlayMode:       !byte GeosOverlayNone
GeosActiveMenu:        !byte 0
GeosMenuSelection:     !byte 0
GeosHomeSelection:     !byte 0
GeosControlSelection:  !byte 0
GeosNotice:            !byte 0
GeosShellKey:          !byte 0
GeosWorkSlot:          !byte 0
GeosFolderIndex:       !byte 0
GeosArrangeOrigin:     !byte 0
GeosDragCandidate:     !byte $ff
GeosDragOrigin:        !byte 0
GeosDragTarget:        !byte 0
GeosDragActive:        !byte 0
GeosMouseWasDown:      !byte 0
