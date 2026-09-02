; Pixel-native desktop, using the exact 5x7 font and 24x16 artwork from the
; approved mock. Compose under BASIC, then publish changed bitmap bytes only.
; All drawing pointers are self-modifying absolute operands, not SID-owned ZP.
GeosRichCanvas = $a000

GeosRichBegin:
   lda $01
   sta RichSavedBank
   and #$fe
   sta $01
   rts

GeosRichCompose:
   lda GeosSurfaceMode
   bne RichComposeFiles
   jsr GeosRichHome
   jmp RichComposeChrome
RichComposeFiles:
   jsr GeosRichFileNames
   jsr GeosRichBrowserFooter
RichComposeChrome:
   jsr GeosRichBar
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   jsr GeosRichMenu
+  lda GeosOverlayMode
   cmp #GeosOverlayControl
   bne +
   jsr GeosRichControl
+  jsr GeosRichPublish
   lda RichSavedBank
   sta $01
   rts

; Publish a finished frame; never clear or change the displayed video mode.
GeosRichPublish:
   lda #$a0
   sta RichPublishRead+2
   lda #$20
   sta RichPublishCompare+2
   sta RichPublishWrite+2
   ldx #31
   ldy #0
RichPublishPage:
RichPublishRead:
   lda $a000,y
RichPublishCompare:
   cmp $2000,y
   beq +
RichPublishWrite:
   sta $2000,y
+  iny
   bne RichPublishPage
   inc RichPublishRead+2
   inc RichPublishCompare+2
   inc RichPublishWrite+2
   dex
   bne RichPublishPage
RichPublishTail:
   lda $bf00,y
   cmp $3f00,y
   beq +
   sta $3f00,y
+  iny
   cpy #64
   bne RichPublishTail
   rts

; RichX (9-bit), RichY (pixel): return destination address in absolute operands.
RichAddress:
   lda RichY
   lsr
   lsr
   lsr
   tax
   lda RichX
   and #$f8
   clc
   adc TblGeosBitmapRowLo,x
   sta RichRead+1
   lda TblGeosBitmapRowHi,x
   adc RichXHi
   clc
   adc #$80
   sta RichRead+2
   lda RichY
   and #7
   ora RichRead+1
   sta RichRead+1
   sta RichWrite+1
   lda RichRead+2
   sta RichWrite+2
   rts

; Apply A mask with RichInk=$ff (black) or 0 (white).
RichApply:
   sta RichMask
RichRead:
   lda $ffff
   eor RichInk
   ora RichMask
   eor RichInk
   ; Replace only masked pixels with RichInk, preserving the other pixels.
   eor RichMask
RichWrite:
   sta $ffff
   rts

RichNextByte:
   clc
   lda RichRead+1
   adc #8
   sta RichRead+1
   sta RichWrite+1
   bcc +
   inc RichRead+2
   inc RichWrite+2
+  rts

; Filled rectangle: X/Y, W/WHi/H, Ink. Width supports the entire 320-pixel row.
; Preserves X/Y; edges are pixel-exact, full middle bytes are handled together.
RichRect:
   lda RichY
   pha
   lda RichX
   and #7
   tax
   lda RichRightMasks,x
   sta RichFirstMask
   lda RichX
   clc
   adc RichW
   sta RichEndX
   lda RichXHi
   adc RichWHi
   sta RichEndHi
   lda RichEndX
   bne +
   dec RichEndHi
+  dec RichEndX
   lda RichEndX
   and #7
   tax
   lda RichLeftMasks,x
   sta RichLastMask
   lda RichEndHi
   lsr
   lda RichEndX
   ror
   lsr
   lsr
   sta RichEndCol
   lda RichXHi
   lsr
   lda RichX
   ror
   lsr
   lsr
   sta RichStartCol
RichRectRow:
   jsr RichAddress
   lda RichEndCol
   sec
   sbc RichStartCol
   sta RichColumns
   lda RichFirstMask
   ldx RichColumns
   bne RichRectFirst
   and RichLastMask
   jsr RichApply
   jmp RichRectNextRow
RichRectFirst:
   jsr RichApply
RichRectByte:
   jsr RichNextByte
   dec RichColumns
   beq RichRectLast
   lda #$ff
   jsr RichApply
   jmp RichRectByte
RichRectLast:
   lda RichLastMask
   jsr RichApply
RichRectNextRow:
   inc RichY
   dec RichH
   bne RichRectRow
   pla
   sta RichY
   rts

; Row-major bitmap: source operand, RichBytes per row, RichH rows; X/Y preserved.
RichBlit:
   lda RichY
   pha
   lda RichX
   and #7
   sta RichShift
RichBlitRow:
   jsr RichAddress
   lda RichBytes
   sta RichColumns
RichBlitByte:
RichSource:
   lda $ffff
   sta RichBits
   lda #0
   sta RichSpill
   ldx RichShift
   beq RichBlitAligned
   lda RichBits
RichShiftLoop:
   lsr
   ror RichSpill
   dex
   bne RichShiftLoop
   sta RichBits
RichBlitAligned:
   lda RichBits
   jsr RichApply
   jsr RichNextByte
   lda RichSpill
   beq +
   jsr RichApply
+  inc RichSource+1
   bne +
   inc RichSource+2
+  dec RichColumns
   bne RichBlitByte
   inc RichY
   dec RichH
   bne RichBlitRow
   pla
   sta RichY
   rts

; A/Y=PETSCII string, pixel X/Y already set. Fixed six-pixel advance, not cells.
RichText:
   sta RichTextRead+1
   sty RichTextRead+2
RichTextLoop:
RichTextRead:
   lda $ffff
   beq RichTextDone
   and #$7f
   cmp #32
   bcc RichTextNext
   jsr RichChar
RichTextNext:
   inc RichTextRead+1
   bne RichTextLoop
   inc RichTextRead+2
   bne RichTextLoop
RichTextDone:
   rts

RichChar:
   pha
   lda RichXHi
   beq +
   lda RichX
   cmp #59
   bcc +
   pla
   rts
+  pla
   sec
   sbc #32
   ldx #0
   stx RichFontHi
   asl
   rol RichFontHi
   asl
   rol RichFontHi
   asl
   rol RichFontHi
   clc
   adc #<GeosRichFont
   sta RichSource+1
   lda RichFontHi
   adc #>GeosRichFont
   sta RichSource+2
   lda #1
   sta RichBytes
   lda #7
   sta RichH
   jsr RichBlit
   clc
   lda RichX
   adc #6
   sta RichX
   bcc +
   inc RichXHi
+  rts

GeosRichHome:
   lda #0
   sta RichClear+1
   lda #$a0
   sta RichClear+2
   ldx #32
   ldy #0
   lda #0
RichClear:
   sta $a000,y
   iny
   bne RichClear
   inc RichClear+2
   dex
   bne RichClear
   ; Standard bitmap, black pixels on white throughout the desktop.
   lda #$01
   ldx #0
-  sta $0400,x
   sta $0500,x
   sta $0600,x
   sta $06e8,x
   inx
   bne -
   lda #20
   sta RichY
RichDotsRow:
   lda RichY
   and #8
   beq +
   lda #4
   bne ++
+  lda #8
++ sta RichX
   lda #0
   sta RichXHi
   lda #$ff
   sta RichInk
RichDotsCol:
   jsr RichAddress
   lda RichX
   and #7
   tax
   lda RichPixelMasks,x
   jsr RichApply
   clc
   lda RichX
   adc #16
   sta RichX
   bcc +
   inc RichXHi
+  lda RichXHi
   beq RichDotsCol
   lda RichX
   cmp #64
   bcc RichDotsCol
   lda RichY
   clc
   adc #8
   sta RichY
   cmp #180
   bcc RichDotsRow
   lda #0
   sta RichItem
RichHomeIconLoop:
   jsr RichHomeIcon
   inc RichItem
   lda RichItem
   cmp #9
   bne RichHomeIconLoop
   ; Two-pixel status separator, like the mock.
   lda #0
   sta RichX
   sta RichXHi
   sta RichWHi
   lda #184
   sta RichY
   lda #64
   sta RichW
   lda #1
   sta RichWHi
   lda #2
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   lda #4
   sta RichX
   lda #189
   sta RichY
   lda GeosOverlayMode
   cmp #GeosOverlayArrange
   bne +
   lda #<RichArrangeText
   ldy #>RichArrangeText
   jmp RichText
+  lda GeosNotice
   beq +
   asl
   tax
   lda TblGeosNotice,x
   ldy TblGeosNotice+1,x
   jmp RichText
+  lda GeosHomeSelection
   asl
   tax
   lda TblGeosHomeStatus,x
   ldy TblGeosHomeStatus+1,x
   jsr RichText
   lda #30
   sta RichX
   lda #1
   sta RichXHi
   lda #<RichIconsText
   ldy #>RichIconsText
   jmp RichText

RichHomeIcon:
   ldx RichItem
   lda TblGeosHomeIconSlot,x
   tax
   lda RichSlotX,x
   sta RichIconX
   sta RichX
   lda RichSlotXHi,x
   sta RichIconXHi
   sta RichXHi
   lda RichSlotY,x
   sta RichIconY
   sta RichY
   lda #0
   sta RichWHi
   sta RichInk
   lda #24
   sta RichW
   lda #16
   sta RichH
   jsr RichRect
   ldx RichItem
   lda RichIconLo,x
   sta RichSource+1
   lda RichIconHi,x
   sta RichSource+2
   lda #3
   sta RichBytes
   lda #16
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichBlit
   lda RichIconY
   clc
   adc #20
   sta RichLabelY
   ldx RichItem
   lda RichLabelLo,x
   sta RichLabelRead+1
   lda RichLabelHi,x
   sta RichLabelRead+2
RichHomeLabel:
   lda #0
   sta RichLength
   ldy #0
RichLabelCount:
RichLabelRead:
   lda $ffff,y
   beq RichLabelCounted
   cmp #$0d
   beq RichLabelCounted
   inc RichLength
   iny
   bne RichLabelCount
RichLabelCounted:
   lda RichLength
   asl
   clc
   adc RichLength
   sta RichHalfWidth
   lda RichIconX
   clc
   adc #12
   sta RichX
   lda RichIconXHi
   adc #0
   sta RichXHi
   lda RichX
   sec
   sbc RichHalfWidth
   sta RichX
   bcs +
   dec RichXHi
+  lda RichLabelY
   sec
   sbc #1
   sta RichY
   lda RichLength
   asl
   clc
   adc RichLength
   asl
   clc
   adc #2
   sta RichW
   lda #0
   sta RichWHi
   sta RichInk
   lda RichItem
   cmp GeosHomeSelection
   bne +
   lda #$ff
   sta RichInk
+  lda #9
   sta RichH
   jsr RichRect
   inc RichX
   bne +
   inc RichXHi
+  lda RichLabelY
   sta RichY
   lda RichInk
   eor #$ff
   sta RichInk
   lda #0
   sta RichLabelIndex
RichLabelChars:
   ldy RichLabelIndex
   cpy RichLength
   beq RichLabelNextLine
RichLabelFetch:
   lda RichLabelRead+1
   sta RichLabelCharRead+1
   lda RichLabelRead+2
   sta RichLabelCharRead+2
RichLabelCharRead:
   lda $ffff,y
   and #$7f
   jsr RichChar
   inc RichLabelIndex
   jmp RichLabelChars
RichLabelNextLine:
   ldy RichLength
   lda RichLabelRead+1
   sta RichLabelEndRead+1
   lda RichLabelRead+2
   sta RichLabelEndRead+2
RichLabelEndRead:
   lda $ffff,y
   beq RichIconDone
   iny
   tya
   clc
   adc RichLabelRead+1
   sta RichLabelRead+1
   bcc +
   inc RichLabelRead+2
+  lda RichLabelY
   clc
   adc #8
   sta RichLabelY
   jmp RichHomeLabel
RichIconDone:
   rts

; Ten letters per line, two lines per icon: full 16-character C64 filenames fit.
GeosRichFileNames:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   lda GeosIECCount
   jmp ++
+  lda rRegNumItemsOnPage+IO1Port
++ cmp #20
   bcc +
   lda #19
+  sta RichFileCount
   lda #0
   sta RichItem
RichFileLoop:
   lda RichItem
   cmp RichFileCount
   bcc +
   jmp RichFilesDone
+
   tax
   lda TblGeosCellRow,x
   clc
   adc #2
   asl
   asl
   asl
   sta RichY
   lda TblGeosCellCol,x
   ldx #0
   stx RichXHi
   asl
   rol RichXHi
   asl
   rol RichXHi
   asl
   rol RichXHi
   sta RichX
   lda #64
   sta RichW
   lda #0
   sta RichWHi
   sta RichInk
   lda #16
   sta RichH
   jsr RichRect
   lda RichX
   clc
   adc #2
   sta RichX
   bcc +
   inc RichXHi
+  lda #$ff
   sta RichInk
   lda RichX
   sta RichFileX
   lda RichXHi
   sta RichFileXHi
   ldx RichItem
   lda TblGeosRichFileLabelLo,x
   sta RichFileRead+1
   lda TblGeosRichFileLabelHi,x
   sta RichFileRead+2
   lda #0
   sta RichFileCharIndex
RichFileCharacters:
   ldy RichFileCharIndex
   cpy #20
   beq RichFileTextDone
RichFileRead:
   lda $ffff,y
   beq RichFileTextDone
   and #$7f
   cmp #32
   bcs +
   lda #32
+  jsr RichChar
   inc RichFileCharIndex
   lda RichFileCharIndex
   cmp #10
   bne RichFileCharacters
   lda RichY
   clc
   adc #8
   sta RichY
   lda RichFileX
   sta RichX
   lda RichFileXHi
   sta RichXHi
   jmp RichFileCharacters
RichFileTextDone:
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   bne +
   lda GeosIECSelection
   jmp ++
+  lda rwRegCursorItemOnPg+IO1Port
++ cmp RichItem
   bne RichFileNormalColor
   ldx #GeosBitmapColorSelected
   jmp RichFileSetColor
RichFileNormalColor:
   ldx #GeosBitmapColorNormal
RichFileSetColor:
   lda RichItem
   jsr GeosBitmapSetItemLabelColor
   inc RichItem
   jmp RichFileLoop
RichFilesDone:
   rts

; A single quiet shortcut strip. Navigation is in the window header only.
GeosRichBrowserFooter:
   lda #0
   sta RichX
   sta RichXHi
   sta RichInk
   lda #184
   sta RichY
   lda #64
   sta RichW
   lda #1
   sta RichWHi
   lda #16
   sta RichH
   jsr RichRect
   lda #190
   sta RichY
   lda #1
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   lda #0
   sta RichItem
RichFunctionLabel:
   ldx RichItem
   lda RichFunctionX,x
   sta RichX
   lda #192
   sta RichY
   lda RichFunctionLo,x
   ldy RichFunctionHi,x
   jsr RichText
   inc RichItem
   lda RichItem
   cmp #5
   bne RichFunctionLabel
   rts

RichFunctionX: !byte 4,76,124,184,244
RichFunctionHitLeft: !byte 2,38,62,92,122
RichFunctionHitRight: !byte 29,53,80,113,146
RichFunctionKey: !byte ChrF1,ChrF3,ChrF5,ChrF7,ChrF8
RichFunctionLo: !byte <RichF1,<RichF3,<RichF5,<RichF7,<RichF8
RichFunctionHi: !byte >RichF1,>RichF3,>RichF5,>RichF7,>RichF8
RichF1: !text "F1 TEENSY",0
RichF3: !text "F3 SD",0
RichF5: !text "F5 USB",0
RichF7: !text "F7 HELP",0
RichF8: !text "F8 PANEL",0

; Eight-pixel top bar keeps existing browser title/path rows accessible.
GeosRichBar:
   lda #0
   sta RichX
   sta RichXHi
   sta RichY
   lda #64
   sta RichW
   lda #1
   sta RichWHi
   lda #8
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   lda #$01
   ldx #39
-  sta $0400,x
   dex
   bpl -
   lda #0
   sta RichWHi
   sta RichItem
RichBarMenu:
   ldx RichItem
   lda RichMenuLeft,x
   sta RichX
   lda RichMenuWidth,x
   sta RichW
   lda #0
   sta RichY
   lda #8
   sta RichH
   lda #$ff
   sta RichInk
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne +
   lda RichItem
   cmp GeosActiveMenu
   bne +
   lda #0
   sta RichInk
   jsr RichRect
+  lda RichInk
   eor #$ff
   sta RichInk
   lda RichX
   clc
   adc #3
   sta RichX
   lda #1
   sta RichY
   ldx RichItem
   lda RichMenuNameLo,x
   ldy RichMenuNameHi,x
   jsr RichText
   inc RichItem
   lda RichItem
   cmp #5
   bne RichBarMenu
   jmp GeosRichClockPaint

GeosRichMenu:
   ldx GeosActiveMenu
   lda RichDropdownLeft,x
   sta RichPanelX
   lda RichDropdownWidth,x
   sta RichPanelW
   lda TblGeosMenuCount,x
   sta RichMenuCount
   asl
   clc
   adc RichMenuCount
   asl
   asl
   clc
   adc #4
   sta RichPanelH
   lda #8
   sta RichPanelY
   jsr RichPanel
   lda #0
   sta RichItem
RichDropdownItem:
   lda RichItem
   asl
   sta RichTemp
   asl
   clc
   adc RichTemp
   asl
   clc
   adc #11
   sta RichY
   lda RichPanelX
   clc
   adc #4
   sta RichX
   lda #0
   sta RichXHi
   sta RichWHi
   lda #$ff
   sta RichInk
   lda RichItem
   cmp GeosMenuSelection
   bne +
   lda RichPanelW
   sec
   sbc #8
   sta RichW
   lda #9
   sta RichH
   jsr RichRect
   lda #0
   sta RichInk
+  inc RichY
   ldx GeosActiveMenu
   lda TblGeosMenuListLo,x
   sta RichMenuPtr+1
   lda TblGeosMenuListHi,x
   sta RichMenuPtr+2
   lda RichItem
   asl
   tay
RichMenuPtr:
   lda $ffff,y
   sta RichStringLo
   iny
   lda RichMenuPtr+1
   sta RichMenuPtrHi+1
   lda RichMenuPtr+2
   sta RichMenuPtrHi+2
RichMenuPtrHi:
   lda $ffff,y
   tay
   lda RichStringLo
   jsr RichText
   inc RichItem
   lda RichItem
   cmp RichMenuCount
   beq +
   jmp RichDropdownItem
+
   rts

; Pixel frame with one-pixel keyline and inset white body.
RichPanel:
   lda RichPanelX
   sta RichX
   lda #0
   sta RichXHi
   sta RichWHi
   lda RichPanelY
   sta RichY
   lda RichPanelW
   sta RichW
   lda RichPanelH
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   inc RichX
   inc RichY
   lda RichPanelW
   sec
   sbc #2
   sta RichW
   lda RichPanelH
   sec
   sbc #2
   sta RichH
   lda #0
   sta RichInk
   jsr RichRect
   ; Normalize every color cell touched by this black/white panel.
   lda RichPanelY
   lsr
   lsr
   lsr
   sta RichColorRow
   lda RichPanelY
   clc
   adc RichPanelH
   sec
   sbc #1
   lsr
   lsr
   lsr
   sta RichColorLast
RichPanelColorRow:
   ldx RichColorRow
   lda TblGeosBitmapScreenRowLo,x
   sta RichColorWrite+1
   lda TblGeosBitmapScreenRowHi,x
   sta RichColorWrite+2
   lda RichPanelX
   lsr
   lsr
   lsr
   tax
   lda RichPanelX
   clc
   adc RichPanelW
   sta RichTemp
   lda #0
   adc #0
   lsr
   lda RichTemp
   ror
   lsr
   lsr
   tay
   lda #$01
RichColorWrite:
   sta $ffff,x
   inx
   dey
   ; Compare against end column without losing the constant ink color.
   txa
   pha
   lda RichPanelX
   clc
   adc RichPanelW
   sta RichTemp
   lda #0
   adc #0
   lsr
   lda RichTemp
   ror
   lsr
   lsr
   sta RichColorEnd
   pla
   cmp RichColorEnd
   lda #$01
   bcc RichColorWrite
   inc RichColorRow
   lda RichColorLast
   cmp RichColorRow
   bcc +
   jmp RichPanelColorRow
+
   rts

GeosRichControl:
   ; Retain the established eight category targets (rows 5..12).
   lda #16
   sta RichPanelX
   lda #24
   sta RichPanelY
   lda #240
   sta RichPanelW
   lda #144
   sta RichPanelH
   jsr RichPanel
   lda #18
   sta RichX
   lda #26
   sta RichY
   lda #236
   sta RichW
   lda #10
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   lda #86
   sta RichX
   lda #28
   sta RichY
   lda #0
   sta RichInk
   lda #<RichControlTitle
   ldy #>RichControlTitle
   jsr RichText
   lda #0
   sta RichItem
RichControlRow:
   lda RichItem
   asl
   asl
   asl
   clc
   adc #40
   sta RichY
   lda #36
   sta RichX
   lda #$ff
   sta RichInk
   lda RichItem
   cmp GeosControlSelection
   bne +
   lda #212
   sta RichW
   lda #8
   sta RichH
   jsr RichRect
   lda #0
   sta RichInk
+  lda RichItem
   asl
   tax
   lda TblGeosControlLabel,x
   ldy TblGeosControlLabel+1,x
   jsr RichText
   inc RichItem
   lda RichItem
   cmp #8
   bne RichControlRow
   lda #28
   sta RichX
   lda #124
   sta RichY
   lda #$ff
   sta RichInk
   lda #<RichControlHelp
   ldy #>RichControlHelp
   jsr RichText
   lda #28
   sta RichX
   lda #144
   sta RichY
   lda #<RichFirmwareHelp
   ldy #>RichFirmwareHelp
   jmp RichText

; Native clock and play/pause control, updated only when time/state changes.
GeosRichClock:
   jsr RichClockSnapshot
   lda RichClockSecond
   cmp RichLastSecond
   bne RichClockRefresh
   lda RichClockMinute
   cmp RichLastMinute
   bne RichClockRefresh
   lda RichClockHour
   cmp RichLastHour
   bne RichClockRefresh
   lda RichClockFormat
   cmp RichLastFormat
   bne RichClockRefresh
   lda RichClockSID
   cmp RichLastSID
   bne RichClockRefresh
   rts
RichClockRefresh:
   jsr GeosRichBegin
   jsr RichClockPaintSnapshot
   ; Only header bytes 224..319 changed; do not republish an old body.
   ldx #0
-  lda $a0e0,x
   cmp $20e0,x
   beq +
   sta $20e0,x
+  inx
   cpx #96
   bne -
   lda RichSavedBank
   sta $01
   rts

GeosRichClockPaint:
   jsr RichClockSnapshot
RichClockPaintSnapshot:
   lda RichClockSecond
   sta RichLastSecond
   lda RichClockMinute
   sta RichLastMinute
   lda RichClockHour
   sta RichLastHour
   lda RichClockFormat
   sta RichLastFormat
   lda RichClockSID
   sta RichLastSID
   lda #224
   sta RichX
   lda #0
   sta RichXHi
   sta RichY
   sta RichWHi
   lda #96
   sta RichW
   lda #8
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   lda #232
   sta RichX
   lda #0
   sta RichInk
   sta RichY
   lda #<RichPause
   ldy #>RichPause
   ldx RichClockSID
   beq +
   lda #<RichPlay
   ldy #>RichPlay
+  sta RichSource+1
   sty RichSource+2
   lda #1
   sta RichBytes
   lda #8
   sta RichH
   jsr RichBlit
   lda #4
   sta RichX
   lda #1
   sta RichXHi
   sta RichY
   lda RichClockFormat
   beq RichClock12
   lda RichClockHour
   and #$1f
   cmp #$12
   bne +
   lda #0
+  bit RichClockHour
   bpl RichClockHourReady
   php
   sei
   sed
   clc
   adc #$12
   cld
   plp
   jmp RichClockHourReady
RichClock12:
   lda RichClockHour
   and #$1f
   bne RichClockHourReady
   lda #$12
RichClockHourReady:
   jsr RichHexByte
   lda #':'
   jsr RichChar
   lda RichClockMinute
   jsr RichHexByte
   lda #':'
   jsr RichChar
   lda RichClockSecond
   jsr RichHexByte
   lda RichClockFormat
   bne RichClockDone
   lda #'A'
   bit RichClockHour
   bpl +
   lda #'P'
+  and #$7f                  ;PETSCII A/P literals -> native ASCII font index
   jsr RichChar
RichClockDone:
   rts

; Read hours first to latch one complete CIA TOD value, then promptly release
; the read latch with tenths. Rendering never holds the latch across SID IRQs.
RichClockSnapshot:
   php
   sei
   lda TODTenthSecBCD
   lda TODHoursBCD
   sta RichClockHour
   lda TODMinBCD
   sta RichClockMinute
   lda TODSecBCD
   sta RichClockSecond
   lda TODTenthSecBCD
   lda smc24HourClockDisp+1
   sta RichClockFormat
   lda smcSIDPauseStop+1
   sta RichClockSID
   plp
   rts
RichHexByte:
   pha
   lsr
   lsr
   lsr
   lsr
   clc
   adc #'0'
   jsr RichChar
   pla
   and #$0f
   clc
   adc #'0'
   jmp RichChar

; Install the mock font in legacy 8px browser cells; desktop text uses 6px.
GeosRichInstallFont:
   lda #0
   sta RichItem
RichInstallGlyph:
   lda RichItem
   ;Keep original punctuation absent from the mock font ([X], < >, arrows).
   cmp #27
   bcc RichInstallMapped
   cmp #32
   bcc RichInstallNext
   cmp #$3c
   beq RichInstallNext
   cmp #$3e
   beq RichInstallNext
   cmp #$3d
   beq RichInstallNext
   cmp #$2a
   beq RichInstallNext
   cmp #$28
   beq RichInstallNext
   cmp #$29
   beq RichInstallNext
RichInstallMapped:
   lda RichItem
   cmp #32
   bcs +
   clc
   adc #64
+  sec
   sbc #32
   ldx #0
   stx RichFontHi
   asl
   rol RichFontHi
   asl
   rol RichFontHi
   asl
   rol RichFontHi
   clc
   adc #<GeosRichFont
   sta RichFontRead+1
   lda RichFontHi
   adc #>GeosRichFont
   sta RichFontRead+2
   lda RichItem
   ldx #0
   stx RichFontHi
   asl
   rol RichFontHi
   asl
   rol RichFontHi
   asl
   rol RichFontHi
   sta RichFontWrite+1
   lda RichFontHi
   clc
   adc #>GeosCharsetRAM
   sta RichFontWrite+2
   ldy #7
RichFontRead:
   lda $ffff,y
   lsr
RichFontWrite:
   sta $ffff,y
   dey
   bpl RichFontRead
RichInstallNext:
   inc RichItem
   lda RichItem
   cmp #92
   beq +
   jmp RichInstallGlyph
+
   rts

RichRightMasks: !byte $ff,$7f,$3f,$1f,$0f,$07,$03,$01
RichLeftMasks: !byte $80,$c0,$e0,$f0,$f8,$fc,$fe,$ff
RichPixelMasks: !byte $80,$40,$20,$10,$08,$04,$02,$01
RichSlotX: !byte 16,76,136,196,0,16,76,136,196,0,16,76,136,196,0
RichSlotXHi: !byte 0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
RichSlotY: !byte 28,28,28,28,28,82,82,82,82,82,136,136,136,136,136
RichIconLo: !for i,0,8 { !byte <(GeosRichIcons+i*48) }
RichIconHi: !for i,0,8 { !byte >(GeosRichIcons+i*48) }
RichLabelLo: !byte <RichTeensy,<RichSD,<RichUSB,<RichDrive8,<RichDrive9,<RichGames,<RichUtilities,<RichControl,<RichTrash
RichLabelHi: !byte >RichTeensy,>RichSD,>RichUSB,>RichDrive8,>RichDrive9,>RichGames,>RichUtilities,>RichControl,>RichTrash
RichMenuLeft: !byte 0,48,80,112,144
RichMenuWidth: !byte 48,32,32,32,32
RichDropdownLeft: !byte 0,48,80,112,144
RichDropdownWidth: !byte 120,128,128,112,136
RichMenuNameLo: !byte <RichTeensyName,<RichFileName,<RichEditName,<RichViewName,<RichDiskName
RichMenuNameHi: !byte >RichTeensyName,>RichFileName,>RichEditName,>RichViewName,>RichDiskName
RichTeensyName: !text "TEENSY",0
RichFileName: !text "FILE",0
RichEditName: !text "EDIT",0
RichViewName: !text "VIEW",0
RichDiskName: !text "DISK",0
RichTeensy: !text "TEENSY",0
RichSD: !text "SD CARD",0
RichUSB: !text "USB",0
RichDrive8: !text "DRIVE 8",0
RichDrive9: !text "DRIVE 9",0
RichGames: !text "GAMES",0
RichUtilities: !text "UTILITIES",0
RichControl: !text "CONTROL",13,"PANEL",0
RichTrash: !text "TRASH",0
RichIconsText: !text "ICONS",0
RichArrangeText: !text "MOVE ICON: ARROWS   RETURN: SAVE",0
RichControlTitle: !text "CONTROL PANEL",0
RichControlHelp: !text "DOUBLE CLICK A CATEGORY TO OPEN",0
RichFirmwareHelp: !text "FIRMWARE UPDATE: FILE MENU",0
RichPlay: !byte $20,$30,$38,$3c,$38,$30,$20,0
RichPause: !byte $6c,$6c,$6c,$6c,$6c,$6c,$6c,0
RichSavedBank: !byte 0
RichX: !byte 0
RichXHi: !byte 0
RichY: !byte 0
RichW: !byte 0
RichWHi: !byte 0
RichH: !byte 0
RichInk: !byte 0
RichMask: !byte 0
RichFirstMask: !byte 0
RichLastMask: !byte 0
RichEndX: !byte 0
RichEndHi: !byte 0
RichStartCol: !byte 0
RichEndCol: !byte 0
RichColumns: !byte 0
RichBytes: !byte 0
RichShift: !byte 0
RichBits: !byte 0
RichSpill: !byte 0
RichFontHi: !byte 0
RichItem: !byte 0
RichIconX: !byte 0
RichIconXHi: !byte 0
RichIconY: !byte 0
RichLabelY: !byte 0
RichLength: !byte 0
RichHalfWidth: !byte 0
RichLabelIndex: !byte 0
RichStringLo: !byte 0
RichMenuCount: !byte 0
RichPanelX: !byte 0
RichPanelY: !byte 0
RichPanelW: !byte 0
RichPanelH: !byte 0
RichTemp: !byte 0
RichColorRow: !byte 0
RichColorLast: !byte 0
RichColorEnd: !byte 0
RichLastSecond: !byte $ff
RichLastMinute: !byte $ff
RichLastHour: !byte $ff
RichLastFormat: !byte $ff
RichLastSID: !byte $ff
RichClockHour: !byte 0
RichClockMinute: !byte 0
RichClockSecond: !byte 0
RichClockFormat: !byte 0
RichClockSID: !byte 0
RichFileCount: !byte 0
RichFileX: !byte 0
RichFileXHi: !byte 0
RichFileCharIndex: !byte 0
