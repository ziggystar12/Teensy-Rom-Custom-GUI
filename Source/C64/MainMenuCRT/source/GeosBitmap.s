; True 320x200 standard high-resolution bitmap renderer for DesktopShell.
;
; The established character-layout routines remain the single source of truth
; for text, icons, menus, and hit boxes.  After each complete redraw this module
; snapshots the temporary RAM font, expands all 1,000 cells into the 8,000-byte
; bitmap at $2000, and turns the $0400 screen matrix into two-color cell data.
; Bit 4 of $d016 stays clear: this is 320-pixel standard hi-res, not 160-pixel
; multicolor mode.

   GeosBitmapRAM = $2000
   GeosBitmapRAMEnd = $3f40
   GeosBitmapScreen = C64ScreenRAM

   ;Screen-byte nibbles are foreground/background in standard bitmap mode.
   GeosBitmapColorNormal = $01     ;black on white
   GeosBitmapColorAccent = $61     ;blue on white
   GeosBitmapColorSelected = $16   ;white on blue
   GeosBitmapColorClock = $76      ;yellow on blue
   GeosBitmapColorStatus = $0f     ;black on light grey

; Convert the complete temporary character surface into a real VIC-II bitmap.
GeosBitmapConvertScreen:
   jsr Mouse1351HideForRedraw
   jsr GeosBitmapCaptureFont

   ;Keep the VIC in bank 0, where screen $0400 and bitmap $2000 reside.
   lda $dd02
   ora #%00000011
   sta $dd02
   lda $dd00
   ora #%00000011
   sta $dd00

   ;Blank the display during the one-frame rasterization pass.
   lda $d011
   and #%11101111
   sta $d011
   lda #0
   sta GeosBitmapActive
   sta GeosBitmapRow

GeosBitmapConvertRow:
   ldx GeosBitmapRow
   lda TblGeosBitmapScreenRowLo,x
   sta smcGeosBitmapReadCell+1
   sta smcGeosBitmapWriteCell+1
   lda TblGeosBitmapScreenRowHi,x
   sta smcGeosBitmapReadCell+2
   sta smcGeosBitmapWriteCell+2
   lda #0
   sta GeosBitmapCol

GeosBitmapConvertCell:
   ldx GeosBitmapCol
smcGeosBitmapReadCell:
   lda $ffff,x
   sta GeosBitmapChar
   and #$7f
   sta GeosBitmapScreenCode
   php
   sei
   jsr GeosBitmapSetFontPointer
   jsr GeosBitmapSetCellPointer

   ldy #0
GeosBitmapCopyGlyph:
   lda (PtrAddrLo),y
   sta (Ptr2AddrLo),y
   iny
   cpy #8
   bne GeosBitmapCopyGlyph
   plp

   lda GeosBitmapChar
   bmi GeosBitmapCellSelected
   lda GeosBitmapScreenCode
   cmp #GeosHomeIconFirst
   bcc GeosBitmapCellNormal
   cmp #GeosHomeIconFirst+28
   bcs GeosBitmapCellNormal
   lda #GeosBitmapColorAccent
   bne GeosBitmapStoreCellColor
GeosBitmapCellSelected:
   lda #GeosBitmapColorSelected
   bne GeosBitmapStoreCellColor
GeosBitmapCellNormal:
   lda #GeosBitmapColorNormal
GeosBitmapStoreCellColor:
   ldx GeosBitmapCol
smcGeosBitmapWriteCell:
   sta $ffff,x

   inc GeosBitmapCol
   lda GeosBitmapCol
   cmp #40
   bne GeosBitmapConvertCell
   inc GeosBitmapRow
   lda GeosBitmapRow
   cmp #25
   bne GeosBitmapConvertRow

   jsr GeosBitmapTintSurface

   ;$0400 is the screen matrix, $2000 is the bitmap, and multicolor stays off.
   lda #MouseSpritePointerValue
   sta Sprite0Pointer
   lda #$18
   sta VICMemSetup
   lda #$c8
   sta $d016
   lda #PokeBlue
   sta BorderColorReg
   lda #PokeWhite
   sta BackgndColorReg
   lda #$3b
   sta $d011
   lda #0
   sta GeosBitmapLayoutPass
   lda #1
   sta GeosBitmapActive
   jsr GeosBitmapRefreshBrowserSelection
   jsr GeosBitmapDisplayTime
   rts

; Snapshot all 256 glyphs before the bitmap expansion overwrites $3800-$3f3f.
GeosBitmapCaptureFont:
   ldx #0
GeosBitmapCaptureFontLoop:
   lda GeosCharsetRAM+$000,x
   sta GeosBitmapFontData+$000,x
   lda GeosCharsetRAM+$100,x
   sta GeosBitmapFontData+$100,x
   lda GeosCharsetRAM+$200,x
   sta GeosBitmapFontData+$200,x
   lda GeosCharsetRAM+$300,x
   sta GeosBitmapFontData+$300,x
   lda GeosCharsetRAM+$400,x
   sta GeosBitmapFontData+$400,x
   lda GeosCharsetRAM+$500,x
   sta GeosBitmapFontData+$500,x
   lda GeosCharsetRAM+$600,x
   sta GeosBitmapFontData+$600,x
   lda GeosCharsetRAM+$700,x
   sta GeosBitmapFontData+$700,x
   inx
   bne GeosBitmapCaptureFontLoop
   rts

; GeosBitmapScreenCode selects one of the captured 8-byte glyphs.
GeosBitmapSetFontPointer:
   lda #0
   sta GeosBitmapFontOffsetHi
   lda GeosBitmapScreenCode
   asl
   rol GeosBitmapFontOffsetHi
   asl
   rol GeosBitmapFontOffsetHi
   asl
   rol GeosBitmapFontOffsetHi
   clc
   adc #<GeosBitmapFontData
   sta PtrAddrLo
   lda GeosBitmapFontOffsetHi
   adc #>GeosBitmapFontData
   sta PtrAddrHi
   rts

; GeosBitmapRow/Col select one 8x8 bitmap cell.
GeosBitmapSetCellPointer:
   ldx GeosBitmapRow
   lda TblGeosBitmapRowLo,x
   sta Ptr2AddrLo
   lda TblGeosBitmapRowHi,x
   sta Ptr2AddrHi
   lda #0
   sta GeosBitmapCellOffsetHi
   lda GeosBitmapCol
   asl
   rol GeosBitmapCellOffsetHi
   asl
   rol GeosBitmapCellOffsetHi
   asl
   rol GeosBitmapCellOffsetHi
   clc
   adc Ptr2AddrLo
   sta Ptr2AddrLo
   lda GeosBitmapCellOffsetHi
   adc Ptr2AddrHi
   sta Ptr2AddrHi
   rts

; Give the status strip its own restrained color pair.  Reverse-video menu and
; selection cells retain their blue palette from the conversion loop.
GeosBitmapTintSurface:
   lda GeosSurfaceMode
   bne GeosBitmapTintBrowser
   ldx #23
   lda #GeosBitmapColorStatus
   jmp GeosBitmapTintRow
GeosBitmapTintBrowser:
   ldx #19
   lda #GeosBitmapColorStatus
   jsr GeosBitmapTintRow
   ldx #20
   lda #GeosBitmapColorStatus
GeosBitmapTintRow:
   sta GeosBitmapColor
   lda TblGeosBitmapScreenRowLo,x
   sta smcGeosBitmapTintWrite+1
   lda TblGeosBitmapScreenRowHi,x
   sta smcGeosBitmapTintWrite+2
   ldx #39
   lda GeosBitmapColor
smcGeosBitmapTintWrite:
   sta $ffff,x
   dex
   bpl smcGeosBitmapTintWrite
   rts

; ---------------------------------------------------------------------------
; Bitmap-native text primitives used after screen RAM becomes color data.

; X=row, Y=column.
GeosBitmapSetCursor:
   stx GeosBitmapRow
   sty GeosBitmapCol
   rts

; A=PETSCII/control byte.  Rendering never invokes KERNAL CHROUT, so the
; bottom-right cell cannot scroll away the menu bar.
GeosBitmapPutChar:
   cmp #ChrRvsOn
   bne +
   lda #1
   sta GeosBitmapReverse
   rts
+  cmp #ChrRvsOff
   bne +
   lda #0
   sta GeosBitmapReverse
   rts
+  cmp #ChrReturn
   bne GeosBitmapPutPrintable
   lda #0
   sta GeosBitmapCol
   lda GeosBitmapRow
   cmp #24
   bcs GeosBitmapPutReturn
   inc GeosBitmapRow
GeosBitmapPutReturn:
   rts
GeosBitmapPutPrintable:
   jsr GeosBitmapPetsciiToScreen
   sta GeosBitmapScreenCode
   php
   sei
   jsr GeosBitmapSetFontPointer
   jsr GeosBitmapSetCellPointer
   ldy #0
GeosBitmapPutGlyphLoop:
   lda (PtrAddrLo),y
   sta (Ptr2AddrLo),y
   iny
   cpy #8
   bne GeosBitmapPutGlyphLoop
   plp

   ldx GeosBitmapRow
   lda TblGeosBitmapScreenRowLo,x
   sta smcGeosBitmapPutColor+1
   lda TblGeosBitmapScreenRowHi,x
   sta smcGeosBitmapPutColor+2
   ldx GeosBitmapCol
   lda GeosBitmapReverse
   beq GeosBitmapUseCurrentColor
   lda #GeosBitmapColorSelected
   bne GeosBitmapStorePutColor
GeosBitmapUseCurrentColor:
   lda GeosBitmapColor
smcGeosBitmapPutColor:
GeosBitmapStorePutColor:
   sta $ffff,x

   inc GeosBitmapCol
   lda GeosBitmapCol
   cmp #40
   bcc +
   lda #0
   sta GeosBitmapCol
   lda GeosBitmapRow
   cmp #24
   bcs +
   inc GeosBitmapRow
+  rts

; Convert PETSCII as the KERNAL screen editor does before indexing the font.
GeosBitmapPetsciiToScreen:
   cmp #$20
   bcs +
   clc
   adc #$80
   rts
+  cmp #$40
   bcs +
   rts
+  cmp #$60
   bcs +
   sec
   sbc #$40
   rts
+  cmp #$80
   bcs +
   sec
   sbc #$20
   rts
+  cmp #$a0
   bcs +
   clc
   adc #$40
   rts
+  cmp #$c0
   bcs +
   sec
   sbc #$40
   rts
+  cmp #$ff
   bne +
   lda #$5e
   rts
+  sec
   sbc #$80
   rts

; A/Y=null-terminated string address.
GeosBitmapPrintString:
   sta smcGeosBitmapStringRead+1
   sty smcGeosBitmapStringRead+2
GeosBitmapPrintStringLoop:
smcGeosBitmapStringRead:
   lda $ffff
   beq GeosBitmapPrintStringDone
   jsr GeosBitmapPutChar
   inc smcGeosBitmapStringRead+1
   bne GeosBitmapPrintStringLoop
   inc smcGeosBitmapStringRead+2
   bne GeosBitmapPrintStringLoop
GeosBitmapPrintStringDone:
   rts

; X=row. Clear all 40 cells without KERNAL scrolling.
GeosBitmapBlankLine:
   ldy #0
   jsr GeosBitmapSetCursor
   lda #40
   sta GeosBitmapCount
GeosBitmapBlankLineLoop:
   lda #ChrSpace
   jsr GeosBitmapPutChar
   dec GeosBitmapCount
   bne GeosBitmapBlankLineLoop
   rts

; A=serial-string selector, X=print limit. The remainder is always drained.
GeosBitmapPrintSerialLimited:
   sta rwRegSerialString+IO1Port
   stx GeosBitmapCount
GeosBitmapSerialLoop:
   lda rwRegSerialString+IO1Port
   beq GeosBitmapSerialDone
   jsr GeosBitmapPutChar
   dec GeosBitmapCount
   bne GeosBitmapSerialLoop
GeosBitmapSerialDrain:
   lda rwRegSerialString+IO1Port
   bne GeosBitmapSerialDrain
GeosBitmapSerialDone:
   rts

; Existing Teensy WAIT helpers print through the KERNAL.  If a command starts
; directly from the bitmap desktop, give those transient messages a valid text
; screen; every shell caller redraws the desktop or launches another program
; after the WAIT completes.
GeosBitmapPrepareLegacyWait:
   lda #0
   sta GeosBitmapLayoutPass
   lda GeosBitmapActive
   beq GeosBitmapLegacyWaitReady
   jsr TextScreenMemColor
   lda #ChrToLower
   jsr SendChar
   lda #ChrClear
   jsr SendChar
GeosBitmapLegacyWaitReady:
   rts

; Print unsigned A as decimal without leading zeroes.
GeosBitmapPrintIntByte:
   sta GeosBitmapValue
   lda #0
   sta GeosBitmapDigit
   sta GeosBitmapHundredsPrinted
GeosBitmapHundredsLoop:
   lda GeosBitmapValue
   cmp #100
   bcc GeosBitmapHundredsDone
   sec
   sbc #100
   sta GeosBitmapValue
   inc GeosBitmapDigit
   bne GeosBitmapHundredsLoop
GeosBitmapHundredsDone:
   lda GeosBitmapDigit
   beq GeosBitmapTensStart
   clc
   adc #'0'
   jsr GeosBitmapPutChar
   lda #1
   sta GeosBitmapHundredsPrinted
GeosBitmapTensStart:
   lda #0
   sta GeosBitmapDigit
GeosBitmapTensLoop:
   lda GeosBitmapValue
   cmp #10
   bcc GeosBitmapTensDone
   sec
   sbc #10
   sta GeosBitmapValue
   inc GeosBitmapDigit
   bne GeosBitmapTensLoop
GeosBitmapTensDone:
   lda GeosBitmapDigit
   bne GeosBitmapPrintTens
   lda GeosBitmapHundredsPrinted
   beq GeosBitmapPrintOnes
GeosBitmapPrintTens:
   lda GeosBitmapDigit
   clc
   adc #'0'
   jsr GeosBitmapPutChar
GeosBitmapPrintOnes:
   lda GeosBitmapValue
   clc
   adc #'0'
   jsr GeosBitmapPutChar
   rts

; ---------------------------------------------------------------------------
; Live browser selection and status updates.

GeosBitmapRefreshBrowserSelection:
   lda GeosBitmapActive
   beq GeosBitmapSelectionDone
   lda GeosSurfaceMode
   beq GeosBitmapSelectionDone
   lda GeosOverlayMode
   bne GeosBitmapSelectionDone
   lda #0
   sta GeosBitmapItem
GeosBitmapClearSelectionLoop:
   lda GeosBitmapItem
   cmp rRegNumItemsOnPage+IO1Port
   bcs GeosBitmapSelectCurrent
   ldx #GeosBitmapColorNormal
   jsr GeosBitmapSetItemLabelColor
   inc GeosBitmapItem
   jmp GeosBitmapClearSelectionLoop
GeosBitmapSelectCurrent:
   lda rRegNumItemsOnPage+IO1Port
   beq GeosBitmapSelectionDone
   lda rwRegCursorItemOnPg+IO1Port
   cmp rRegNumItemsOnPage+IO1Port
   bcc +
   lda #0
   sta rwRegCursorItemOnPg+IO1Port
+  ldx #GeosBitmapColorSelected
   jsr GeosBitmapSetItemLabelColor
GeosBitmapSelectionDone:
   rts

; A=item, X=color byte. Labels occupy eight cells two rows below each icon.
GeosBitmapSetItemLabelColor:
   sta GeosBitmapItem
   stx GeosBitmapColor
   tax
   lda TblGeosCellRow,x
   clc
   adc #2
   tax
   lda TblGeosBitmapScreenRowLo,x
   sta smcGeosBitmapLabelColor+1
   lda TblGeosBitmapScreenRowHi,x
   sta smcGeosBitmapLabelColor+2
   ldx GeosBitmapItem
   lda TblGeosCellCol,x
   tax
   lda GeosBitmapColor
   ldy #8
GeosBitmapLabelColorLoop:
smcGeosBitmapLabelColor:
   sta $ffff,x
   inx
   dey
   bne GeosBitmapLabelColorLoop
   rts

GeosBitmapDrawBrowserStatus:
   jsr GeosBitmapRefreshBrowserSelection
   lda rRegNumItemsOnPage+IO1Port
   bne +
   rts
+  lda rwRegCursorItemOnPg+IO1Port
   cmp rRegNumItemsOnPage+IO1Port
   bcc +
   lda #0
   sta rwRegCursorItemOnPg+IO1Port
+  sta rwRegSelItemOnPage+IO1Port
   lda rRegItemTypePlusIOH+IO1Port
   sta GeosWorkFlags
   and #$7f
   sta GeosWorkType

   lda #0
   sta GeosBitmapReverse
   lda #GeosBitmapColorStatus
   sta GeosBitmapColor
   ldx #19
   jsr GeosBitmapBlankLine
   ldx #20
   jsr GeosBitmapBlankLine

   ldx #19
   ldy #0
   jsr GeosBitmapSetCursor
   lda #<MsgGeosSelected
   ldy #>MsgGeosSelected
   jsr GeosBitmapPrintString
   lda #rsstItemName
   ldx #38
   jsr GeosBitmapPrintSerialLimited

   ldx #20
   ldy #0
   jsr GeosBitmapSetCursor
   lda #<MsgGeosType
   ldy #>MsgGeosType
   jsr GeosBitmapPrintString
   lda GeosWorkType
   asl
   asl
   clc
   adc #1
   sta GeosBitmapTypeIndex
   lda #3
   sta GeosBitmapCount
GeosBitmapTypeLoop:
   ldy GeosBitmapTypeIndex
   lda TblItemType,y
   jsr GeosBitmapPutChar
   inc GeosBitmapTypeIndex
   dec GeosBitmapCount
   bne GeosBitmapTypeLoop

   lda GeosWorkFlags
   bpl +
   lda #<MsgGeosHandler
   ldy #>MsgGeosHandler
   jsr GeosBitmapPrintString
+  lda #<MsgGeosItem
   ldy #>MsgGeosItem
   jsr GeosBitmapPrintString
   lda rwRegCursorItemOnPg+IO1Port
   clc
   adc #1
   jsr GeosBitmapPrintIntByte
   lda #'/'
   jsr GeosBitmapPutChar
   lda rRegNumItemsOnPage+IO1Port
   jsr GeosBitmapPrintIntByte
   lda #<MsgGeosPageStatus
   ldy #>MsgGeosPageStatus
   jsr GeosBitmapPrintString
   lda rwRegPageNumber+IO1Port
   jsr GeosBitmapPrintIntByte
   lda #'/'
   jsr GeosBitmapPutChar
   lda rRegNumPages+IO1Port
   jsr GeosBitmapPrintIntByte
   rts

; ---------------------------------------------------------------------------
; Bitmap-native live RTC clock. It mirrors DisplayTime's 12/24-hour behavior.

GeosBitmapDisplayTime:
   ldx #0
   ldy #30
   jsr GeosBitmapSetCursor
   lda #0
   sta GeosBitmapReverse
   lda #GeosBitmapColorClock
   sta GeosBitmapColor
   lda TODHoursBCD
   sta GeosBitmapClockHour
   ldx smc24HourClockDisp+1
   beq GeosBitmapClock12

   lda #ChrSpace
   jsr GeosBitmapPutChar
   lda #ChrSpace
   jsr GeosBitmapPutChar
   lda GeosBitmapClockHour
   cmp #$12
   bne +
   lda #$00
   beq GeosBitmapClockHourReady
+  cmp #$92
   beq +
   and #$80
   beq +
   lda GeosBitmapClockHour
   and #$1f
   php
   sei
   sed
   clc
   adc #$12
   cld
   plp
   jmp GeosBitmapClockHourReady
+  lda GeosBitmapClockHour
   and #$1f
GeosBitmapClockHourReady:
   jsr GeosBitmapPrintHexByte
   jmp GeosBitmapClockMinutes

GeosBitmapClock12:
   lda GeosBitmapClockHour
   and #$1f
   bne +
   lda GeosBitmapClockHour
   ora #$12
   sta GeosBitmapClockHour
+  lda GeosBitmapClockHour
   and #$10
   bne GeosBitmapClockLeadingOne
   lda #ChrSpace
   bne GeosBitmapClockFirstDigit
GeosBitmapClockLeadingOne:
   lda #'1'
GeosBitmapClockFirstDigit:
   jsr GeosBitmapPutChar
   lda GeosBitmapClockHour
   and #$0f
   jsr GeosBitmapPrintHexNibble

GeosBitmapClockMinutes:
   lda #':'
   jsr GeosBitmapPutChar
   lda TODMinBCD
   jsr GeosBitmapPrintHexByte
   lda #':'
   jsr GeosBitmapPutChar
   lda TODSecBCD
   jsr GeosBitmapPrintHexByte
   lda TODTenthSecBCD
   ldx smc24HourClockDisp+1
   bne GeosBitmapClockDone
   lda GeosBitmapClockHour
   and #$80
   bne GeosBitmapClockPM
   lda #'a'
   bne GeosBitmapClockSuffix
GeosBitmapClockPM:
   lda #'p'
GeosBitmapClockSuffix:
   jsr GeosBitmapPutChar
   lda #'m'
   jsr GeosBitmapPutChar
GeosBitmapClockDone:
   rts

GeosBitmapPrintHexByte:
   pha
   lsr
   lsr
   lsr
   lsr
   jsr GeosBitmapPrintHexNibble
   pla
   and #$0f
GeosBitmapPrintHexNibble:
   clc
   adc #'0'
   jmp GeosBitmapPutChar

; Row bases for bitmap bytes and their matching screen color cells.
TblGeosBitmapRowLo:
   !byte <$2000,<$2140,<$2280,<$23c0,<$2500,<$2640,<$2780,<$28c0
   !byte <$2a00,<$2b40,<$2c80,<$2dc0,<$2f00,<$3040,<$3180,<$32c0
   !byte <$3400,<$3540,<$3680,<$37c0,<$3900,<$3a40,<$3b80,<$3cc0,<$3e00
TblGeosBitmapRowHi:
   !byte >$2000,>$2140,>$2280,>$23c0,>$2500,>$2640,>$2780,>$28c0
   !byte >$2a00,>$2b40,>$2c80,>$2dc0,>$2f00,>$3040,>$3180,>$32c0
   !byte >$3400,>$3540,>$3680,>$37c0,>$3900,>$3a40,>$3b80,>$3cc0,>$3e00
TblGeosBitmapScreenRowLo:
   !byte <$0400,<$0428,<$0450,<$0478,<$04a0,<$04c8,<$04f0,<$0518
   !byte <$0540,<$0568,<$0590,<$05b8,<$05e0,<$0608,<$0630,<$0658
   !byte <$0680,<$06a8,<$06d0,<$06f8,<$0720,<$0748,<$0770,<$0798,<$07c0
TblGeosBitmapScreenRowHi:
   !byte >$0400,>$0428,>$0450,>$0478,>$04a0,>$04c8,>$04f0,>$0518
   !byte >$0540,>$0568,>$0590,>$05b8,>$05e0,>$0608,>$0630,>$0658
   !byte >$0680,>$06a8,>$06d0,>$06f8,>$0720,>$0748,>$0770,>$0798,>$07c0

GeosBitmapActive:           !byte 0
GeosBitmapLayoutPass:       !byte 0
GeosBitmapRow:              !byte 0
GeosBitmapCol:              !byte 0
GeosBitmapChar:             !byte 0
GeosBitmapScreenCode:       !byte 0
GeosBitmapFontOffsetHi:     !byte 0
GeosBitmapCellOffsetHi:     !byte 0
GeosBitmapColor:            !byte GeosBitmapColorNormal
GeosBitmapReverse:          !byte 0
GeosBitmapCount:            !byte 0
GeosBitmapValue:            !byte 0
GeosBitmapDigit:            !byte 0
GeosBitmapHundredsPrinted:  !byte 0
GeosBitmapItem:             !byte 0
GeosBitmapTypeIndex:        !byte 0
GeosBitmapClockHour:        !byte 0

; Protected copy of all 256 glyphs.  It must not live inside $2000-$3f3f,
; because that entire region becomes display data during conversion.
GeosBitmapFontData:
   !fill $800,0
