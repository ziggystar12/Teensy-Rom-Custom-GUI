; True 320x200 standard high-resolution bitmap renderer for DesktopShell.
;
; The layout routines compose text and icons in a protected off-screen canvas.
; The displayed bitmap at $2000 remains visible while changed glyph bytes and
; their two-color pairs at $0400 are applied. Colors are staged in the consumed
; layout at $4000 and published only after the new bitmap. No text-mode pass.
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

; Apply the complete off-screen character surface to the real VIC-II bitmap.
GeosBitmapConvertScreen:
   jsr GeosRichBegin
   ;A new page/surface already rebuilds every label; discard the live cache.
   lda #$ff
   sta GeosBitmapSelectedItem
   lda #>(GeosLayoutScreen-C64ScreenRAM)
   sta GeosBitmapColorOffset

   ;Keep the VIC in bank 0, where screen $0400 and bitmap $2000 reside.
   lda $dd02
   ora #%00000011
   sta $dd02
   lda $dd00
   ora #%00000011
   sta $dd00

   ;The previous bitmap stays visible while the off-screen layout is applied.
   lda #0
   sta GeosBitmapActive
   sta GeosBitmapRow
   lda GeosSurfaceMode
   bne +
   jmp GeosBitmapFinishLayout
+

GeosBitmapConvertRow:
   ldx GeosBitmapRow
   lda TblGeosBitmapScreenRowLo,x
   sta smcGeosBitmapReadCell+1
   sta smcGeosBitmapWriteCell+1
   lda TblGeosBitmapScreenRowHi,x
   clc
   adc #>(GeosLayoutScreen-C64ScreenRAM)
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
   lda Ptr2AddrHi
   clc
   adc #$80
   sta Ptr2AddrHi

   ldy #0
GeosBitmapCopyGlyph:
   lda (PtrAddrLo),y
   cmp (Ptr2AddrLo),y
   beq +
   sta (Ptr2AddrLo),y
+  iny
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
   ; This layout character is consumed; reuse its byte for the pending color.
   sta $ffff,x

   inc GeosBitmapCol
   lda GeosBitmapCol
   cmp #40
   bne GeosBitmapConvertCell
   inc GeosBitmapRow
   lda GeosBitmapRow
   cmp #25
   bne GeosBitmapConvertRow

GeosBitmapFinishLayout:
   jsr GeosBitmapTintSurface
   jsr GeosRichCompose

   ;$0400 is the screen matrix, $2000 is the bitmap, and multicolor stays off.
   lda #MouseSpritePointerValue
   sta Sprite0Pointer
   lda #$18
   sta VICMemSetup
   lda #$c8
   sta $d016
   lda #PokeBlack
   sta BorderColorReg
   lda #PokeWhite
   sta BackgndColorReg
   lda #$3b
   sta $d011
   lda #0
   sta GeosBitmapLayoutPass
   lda #>C64ScreenRAM
   sta $0288
   lda #1
   sta GeosBitmapActive
   jsr GeosBitmapRefreshBrowserSelection
   jsr GeosBitmapDisplayTime
   rts

; Publish only after GeosRichPublish has installed every new bitmap byte.
; Reusing the consumed layout avoids allocating another 1 KiB of desktop RAM.
GeosBitmapPublishColors:
   ldx #0
GeosBitmapPublishColorLoop:
   !for page,0,2 {
      lda GeosLayoutScreen+page*256,x
      cmp C64ScreenRAM+page*256,x
      beq +
      sta C64ScreenRAM+page*256,x
+
   }
   inx
   bne GeosBitmapPublishColorLoop
   ldx #0
GeosBitmapPublishColorTail:
   lda GeosLayoutScreen+768,x
   cmp C64ScreenRAM+768,x
   beq +
   sta C64ScreenRAM+768,x
+  inx
   cpx #232
   bne GeosBitmapPublishColorTail
   lda #0
   sta GeosBitmapColorOffset
   rts

; Font installation now writes directly to protected CPU-only font storage.
GeosBitmapCaptureFont:
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
   cmp #GeosSurfaceIEC
   bne +
   rts                         ;no redundant empty status band on disk views
+
   ldx #19
   lda #GeosBitmapColorStatus
GeosBitmapTintRow:
   sta GeosBitmapColor
   lda TblGeosBitmapScreenRowLo,x
   sta smcGeosBitmapTintWrite+1
   lda TblGeosBitmapScreenRowHi,x
   clc
   adc GeosBitmapColorOffset
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
GeosBitmapPutScreenCode:
   and #$7f
   sta GeosBitmapScreenCode
   php
   sei
   jsr GeosBitmapSetFontPointer
   jsr GeosBitmapSetCellPointer
   ldy #0
GeosBitmapPutGlyphLoop:
   lda (PtrAddrLo),y
   cmp (Ptr2AddrLo),y
   beq +
   sta (Ptr2AddrLo),y
+  iny
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

; Bitmap waits use plain native text, never MsgWaiting's KERNAL color escapes.
; The moving segment means activity only: the backend supplies no byte total.
GeosBitmapWait:
   jsr GeosBitmapWaitBegin
GeosBitmapWaitPoll:
   jsr GeosBitmapWaitAnimate
   lda rwRegStatus+IO1Port
   cmp #rsC64Message
   beq GeosBitmapWaitStable
   cmp #rsReady
   bne GeosBitmapWaitPoll
GeosBitmapWaitStable:
   ldx #5
-  cmp rwRegStatus+IO1Port
   bne GeosBitmapWaitPoll
   dex
   bne -
   cmp #rsReady
   beq GeosBitmapWaitDone
   jsr GeosBitmapWaitMessage
   lda #rsContinue
   sta rwRegStatus+IO1Port
   jmp GeosBitmapWaitPoll
GeosBitmapWaitDone:
   rts

; Draw-only entry also used by the hardware-free desktop preview. The centered
; panel contains the heading, five wrapped message lines and activity track.
GeosBitmapWaitBegin:
   lda #0
   sta GeosBitmapWaitPhase
   lda TODTenthSecBCD
   sta GeosBitmapWaitTick
   php
   sei
   jsr GeosRichBegin
   jsr GeosBitmapWaitPanel
   lda #130
   sta RichX
   lda #65
   sta RichY
   lda #$ff
   sta RichInk
   lda #<MsgGeosLoading
   ldy #>MsgGeosLoading
   jsr RichText
   lda #64
   sta RichX
   lda #148
   sta RichY
   lda #192
   sta RichW
   lda #8
   sta RichH
   jsr RichRect
   jmp GeosBitmapWaitBar

; CIA tenths advance with IRQs disabled during ROM/PRG loading, on PAL and
; NTSC alike. Polling remains nonblocking and does not touch the ready status.
GeosBitmapWaitAnimate:
   lda TODTenthSecBCD
   cmp GeosBitmapWaitTick
   beq GeosBitmapWaitAnimationDone
   sta GeosBitmapWaitTick
   inc GeosBitmapWaitPhase
   lda GeosBitmapWaitPhase
   cmp #21
   bcc +
   lda #0
   sta GeosBitmapWaitPhase
+  php
   sei
   jsr GeosRichBegin
GeosBitmapWaitBar:
   lda #0
   sta RichXHi
   sta RichWHi
   sta RichInk
   lda #66
   sta RichX
   lda #150
   sta RichY
   lda #188
   sta RichW
   lda #4
   sta RichH
   jsr RichRect
   lda GeosBitmapWaitPhase
   asl
   asl
   asl
   clc
   adc #66
   sta RichX
   lda #24
   sta RichW
   lda #4
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
GeosBitmapWaitPublishDone:
   jsr GeosBitmapWaitPublish
   lda RichSavedBank
   sta $01
   plp
GeosBitmapWaitAnimationDone:
   rts

GeosBitmapWaitPanel:
   lda #48
   sta RichPanelX
   lda #56
   sta RichPanelY
   lda #224
   sta RichPanelW
   lda #112
   sta RichPanelH
   jmp RichPanel

; Replace only the heading and activity track on failure. The latest message
; stays in the native canvas, so no panel rebuild can erase it before the key.
GeosBitmapWaitError:
   php
   sei
   jsr GeosRichBegin
   lda #64
   jsr GeosBitmapWaitClearBand
   lda #124
   sta RichX
   lda #65
   sta RichY
   lda #$ff
   sta RichInk
   lda #<MsgGeosLoadStopped
   ldy #>MsgGeosLoadStopped
   jsr RichText
   lda #147
   jsr GeosBitmapWaitClearBand
   jsr GeosBitmapWaitPrompt
   jmp GeosBitmapWaitPublishDone

GeosBitmapWaitPrompt:
   lda #121
   sta RichX
   lda #149
   sta RichY
   lda #$ff
   sta RichInk
   lda #<MsgGeosLoadContinue
   ldy #>MsgGeosLoadContinue
   jmp RichText

; A=top pixel of a ten-pixel band, entirely inside the panel.
GeosBitmapWaitClearBand:
   sta RichY
   lda #0
   sta RichXHi
   sta RichWHi
   sta RichInk
   lda #64
   sta RichX
   lda #192
   sta RichW
   lda #10
   sta RichH
   jmp RichRect

; Publish just the fourteen panel rows, columns6..33. Full-frame publication
; would overwrite live selection and the browser footer outside the panel.
GeosBitmapWaitPublish:
   ldx #7
GeosBitmapWaitPublishRow:
   lda TblGeosBitmapRowLo,x
   clc
   adc #48
   sta GeosBitmapWaitRead+1
   sta GeosBitmapWaitWrite+1
   lda TblGeosBitmapRowHi,x
   adc #0
   sta GeosBitmapWaitWrite+2
   clc
   adc #$80
   sta GeosBitmapWaitRead+2
   ldy #0
GeosBitmapWaitRead:
   lda $ffff,y
GeosBitmapWaitWrite:
   sta $ffff,y
   iny
   cpy #224
   bne GeosBitmapWaitRead
   inx
   cpx #21
   bne GeosBitmapWaitPublishRow
   ; Pixels are complete before the panel's black/white pairs are published.
   ldx #7
GeosBitmapWaitColorRow:
   lda TblGeosBitmapScreenRowLo,x
   sta GeosBitmapWaitColor+1
   lda TblGeosBitmapScreenRowHi,x
   sta GeosBitmapWaitColor+2
   ldy #6
   lda #GeosBitmapColorNormal
GeosBitmapWaitColor:
   sta $ffff,y
   iny
   cpy #34
   bne GeosBitmapWaitColor
   inx
   cpx #21
   bne GeosBitmapWaitColorRow
   rts

; Serial and local messages share the bounded native text renderer. The latest
; message replaces only the body; always drain serial data before rsContinue.
GeosBitmapWaitMessage:
   php
   sei
   jsr GeosRichBegin
   jsr GeosBitmapWaitMessageReset
   lda #rsstSerialStringBuf
   sta rwRegSerialString+IO1Port
GeosBitmapWaitMessageRead:
   lda rwRegSerialString+IO1Port
   beq GeosBitmapWaitMessageDone
   jsr GeosBitmapWaitMessageChar
   jmp GeosBitmapWaitMessageRead
GeosBitmapWaitMessageDone:
   jmp GeosBitmapWaitPublishDone

; A/Y=local zero-terminated PETSCII string. These draw-only entries never read
; Teensy IO or wait for input; callers retain their existing acknowledgement.
GeosBitmapShowMessage:
   sta GeosBitmapWaitLocalRead+1
   sty GeosBitmapWaitLocalRead+2
   php
   sei
   jsr GeosRichBegin
   jsr GeosBitmapWaitPanel
   lda #127
   sta RichX
   lda #65
   sta RichY
   lda #$ff
   sta RichInk
   lda #<MsgGeosInformation
   ldy #>MsgGeosInformation
   jsr RichText
   jsr GeosBitmapWaitPrompt
   jmp GeosBitmapWaitLocalBody

GeosBitmapWaitLocalMessage:
   sta GeosBitmapWaitLocalRead+1
   sty GeosBitmapWaitLocalRead+2
   php
   sei
   jsr GeosRichBegin
GeosBitmapWaitLocalBody:
   jsr GeosBitmapWaitMessageReset
GeosBitmapWaitLocalRead:
   lda $ffff
   beq GeosBitmapWaitMessageDone
   jsr GeosBitmapWaitMessageChar
   inc GeosBitmapWaitLocalRead+1
   bne GeosBitmapWaitLocalRead
   inc GeosBitmapWaitLocalRead+2
   jmp GeosBitmapWaitLocalRead

GeosBitmapWaitMessageReset:
   lda #0
   sta RichXHi
   sta RichWHi
   sta RichInk
   lda #58
   sta RichX
   lda #82
   sta RichY
   lda #204
   sta RichW
   lda #52
   sta RichH
   jsr RichRect
   lda #84
   sta RichY
   lda #$ff
   sta RichInk
   lda #170
   sta GeosBitmapCount
   lda #34
   sta GeosBitmapWaitCol
   rts

; Five rows of 34 six-pixel glyphs, with ten-pixel line spacing. Controls and
; colors are discarded, returns become spaces, and high PETSCII is normalized.
; Once full, consume the rest without drawing beyond the panel.
GeosBitmapWaitMessageChar:
   cmp #ChrReturn
   bne +
   lda GeosBitmapWaitCol
   cmp #34
   beq GeosBitmapWaitMessageCharDone
   lda #ChrSpace
+  cmp #$20
   bcc GeosBitmapWaitMessageCharDone
   cmp #$80
   bcc +
   cmp #$a0
   bcc GeosBitmapWaitMessageCharDone
+  ldx GeosBitmapCount
   beq GeosBitmapWaitMessageCharDone
   and #$7f
GeosBitmapWaitMessageGlyph:
   jsr RichChar
   dec GeosBitmapCount
   dec GeosBitmapWaitCol
   bne GeosBitmapWaitMessageCharDone
   lda #34
   sta GeosBitmapWaitCol
   lda #58
   sta RichX
   lda #0
   sta RichXHi
   lda RichY
   clc
   adc #10
   sta RichY
GeosBitmapWaitMessageCharDone:
   rts

MsgGeosLoading:      !tx "LOADING...",0
MsgGeosLoadStopped:  !tx "LOAD STOPPED",0
MsgGeosLoadContinue: !tx "PRESS ANY KEY",0
MsgGeosInformation:  !tx "INFORMATION",0

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
; Live browser selection changes only the old/new label color pairs. Keep the
; pending palette coherent too, so a later panel-only publish cannot revert it.

GeosBitmapRefreshBrowserSelection:
   lda GeosBitmapActive
   beq GeosBitmapSelectionDone
   lda GeosOverlayMode
   bne GeosBitmapSelectionDone
   lda GeosSurfaceMode
   cmp #GeosSurfaceIEC
   beq GeosBitmapIECSelection
   cmp #GeosSurfaceBrowser
   bne GeosBitmapSelectionDone
   ldx rRegNumItemsOnPage+IO1Port
   lda rwRegCursorItemOnPg+IO1Port
   jmp GeosBitmapSelectionReady
GeosBitmapIECSelection:
   ldx GeosIECCount
   lda GeosIECSelection
GeosBitmapSelectionReady:
   stx GeosBitmapCount
   cmp #MaxDesktopItemsPerPage
   bcs GeosBitmapSelectionDone
   cmp GeosBitmapCount
   bcs GeosBitmapSelectionDone
   cmp GeosBitmapSelectedItem
   beq GeosBitmapSelectionDone
   sta GeosBitmapNewItem
   lda GeosBitmapSelectedItem
   cmp #MaxDesktopItemsPerPage
   bcs GeosBitmapSelectCurrent
   ldx #GeosBitmapColorNormal
   jsr GeosBitmapSetLiveLabelColor
GeosBitmapSelectCurrent:
   lda GeosBitmapNewItem
   sta GeosBitmapSelectedItem
   ldx #GeosBitmapColorSelected
   jsr GeosBitmapSetLiveLabelColor
GeosBitmapSelectionDone:
   rts

GeosBitmapSetLiveLabelColor:
   jsr GeosBitmapSetItemLabelColor
   lda #>(GeosLayoutScreen-C64ScreenRAM)
   sta GeosBitmapColorOffset
   lda GeosBitmapItem
   ldx GeosBitmapColor
   jsr GeosBitmapSetItemLabelColor
   lda #0
   sta GeosBitmapColorOffset
   rts

; A=item, X=color byte. Two eight-cell label rows below each icon.
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
   clc
   adc GeosBitmapColorOffset
   sta smcGeosBitmapLabelColor+2
   lda TblGeosBitmapScreenRowLo,x
   clc
   adc #40
   sta smcGeosBitmapLabelColorSecond+1
   lda smcGeosBitmapLabelColor+2
   adc #0
   sta smcGeosBitmapLabelColorSecond+2
   ldx GeosBitmapItem
   lda TblGeosCellCol,x
   tax
   lda GeosBitmapColor
   ldy #8
GeosBitmapLabelColorLoop:
smcGeosBitmapLabelColor:
   sta $ffff,x
smcGeosBitmapLabelColorSecond:
   sta $ffff,x
   inx
   dey
   bne GeosBitmapLabelColorLoop
   rts

GeosBitmapDrawBrowserStatus:
   jmp GeosBitmapRefreshBrowserSelection

; Retained metadata formatter is not part of the streamlined desktop view.
; Keep its source for the compact-style reference, but do not spend resident
; desktop bytes on an uncalled formatter.
!ifndef DesktopShell {
GeosBitmapLegacyMetadata:
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
}

; ---------------------------------------------------------------------------
; Bitmap-native SID transport and live RTC clock.

GeosBitmapDisplayTime:
   jmp GeosRichClock
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
GeosBitmapColorOffset:      !byte 0
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
GeosBitmapSelectedItem:     !byte $ff
GeosBitmapNewItem:          !byte 0
GeosBitmapTypeIndex:        !byte 0
GeosBitmapWaitCol:          !byte 0
GeosBitmapWaitPhase:        !byte 0
GeosBitmapWaitTick:         !byte 0

; Reverse video uses a color pair, not a second inverted font. Reuse that 1 KiB
; for a page-aligned layout canvas, all inside the SID-protected payload.
GeosBitmapFontData = $4400
