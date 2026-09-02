; SD/USB file operations. The Teensy owns the volatile clipboard and captures
; the delete target before this dialog can send its separate confirmation.
; A modal loop consumes navigation and never dispatches remote launches.
GeosFileCopy:
   lda #rCtlFileCopyWAIT
   bne GeosFileStart
GeosFilePaste:
   lda #rCtlFilePasteWAIT
   bne GeosFileStart
GeosFileDelete:
   lda #rCtlFileDeletePrepareWAIT
GeosFileStart:
   pha
   lda GeosSurfaceMode
   cmp #GeosSurfaceBrowser
   beq +
   jmp GeosFileWrongSurface
+
   lda rWRegCurrMenuWAIT+IO1Port
   cmp #rmtTeensy
   bcc +
   jmp GeosFileWrongSurface
+
   lda rwRegCursorItemOnPg+IO1Port
   sta rwRegSelItemOnPage+IO1Port
   pla
   sta wRegControl+IO1Port
   jsr WaitForTRWaitMsg
   lda #$ff
   sta GeosFileLastState
GeosFileLoop:
   lda rRegFileOpState+IO1Port
   cmp GeosFileLastState
   beq GeosFilePoll
   sta GeosFileLastState
   lda #0
   sta GeosFileChoice
   sta MouseClickEdge
   sta MouseOpenArmed
   sta 198                 ;discard keys queued before the new prompt
   lda Joystick2Sample
   sta GeosFileLastJoy      ;a held launch/fire must be released first
   jsr GeosFileDraw
GeosFilePoll:
   lda GeosFileLastState
   cmp #rfosBusy           ;copy or readback verification in progress
   bne GeosFileReadInput
   lda rRegFileOpProgress+IO1Port
   cmp GeosFileLastProgress
   beq GeosFileReadInput
   sta GeosFileLastProgress
   ldx #14
   jsr GeosBitmapBlankLine
   ldx #14
   ldy #0
   jsr GeosBitmapSetCursor
   lda #rsstFileOpMessage
   ldx #39
   jsr GeosBitmapPrintSerialLimited
   ldx #16
   ldy #5
   jsr GeosBitmapSetCursor
   lda GeosFileLastProgress
   jsr GeosBitmapPrintIntByte
   lda #'%'
   jsr GeosBitmapPutChar
GeosFileReadInput:
   jsr GetIn
   bne GeosFileKey
   jsr GeosFileJoystick
   bne GeosFileKey
   jsr GeosFileMouse
   beq GeosFileLoop
GeosFileKey:
   jsr GeosFileHandleKey
   bcc GeosFileLoop
   lda #0
   sta MouseClickEdge
   sta MouseOpenArmed
   sta GeosMouseWasDown
   jmp GeosShellRedraw
GeosFileWrongSurface:
   pla
   lda #GeosNoticeFileScope
   jmp GeosShellSetNotice

; C set closes a finished dialog. Delete always starts with CANCEL selected.
GeosFileHandleKey:
   cmp #ChrStop
   beq GeosFileCancel
   cmp #27
   beq GeosFileCancel
   cmp #ChrHome
   beq GeosFileCancel
   cmp #'n'
   beq GeosFileCancel
   cmp #'N'
   beq GeosFileCancel
   cmp #ChrReturn
   beq GeosFileEnter
   ldx GeosFileLastState
   cpx #rfosDeleteReady    ;prepared immutable delete target
   bne GeosFileKeyDone
   cmp #'y'
   beq GeosFileConfirm
   cmp #'Y'
   beq GeosFileConfirm
   cmp #ChrCRSRLeft
   beq GeosFileChoose
   cmp #ChrCRSRRight
   beq GeosFileChoose
   cmp #ChrCRSRUp
   beq GeosFileChoose
   cmp #ChrCRSRDn
   bne GeosFileKeyDone
GeosFileChoose:
   lda GeosFileChoice
   eor #1
   sta GeosFileChoice
   jsr GeosFileButtons
GeosFileKeyDone:
   clc
   rts
GeosFileEnter:
   lda GeosFileLastState
   cmp #rfosDeleteReady
   bne GeosFileCancel
   lda GeosFileChoice
   beq GeosFileCancel
GeosFileConfirm:
   lda rRegFileOpState+IO1Port
   cmp #rfosDeleteReady
   bne GeosFileKeyDone
   lda #rCtlFileDeleteConfirmWAIT
   sta wRegControl+IO1Port
   jsr WaitForTRWaitMsg
   jmp GeosFileKeyDone
GeosFileCancel:
   lda GeosFileLastState
   cmp #rfosBusy
   beq +
   cmp #rfosDeleteReady
   beq +
   sec
   rts
+  lda #rCtlFileCancel
   sta wRegControl+IO1Port
   jmp GeosFileKeyDone

GeosFileJoystick:
   lda Joystick2Sample
   cmp GeosFileLastJoy
   beq GeosFileNoInput
   sta GeosFileLastJoy
   and #$10
   beq GeosFileReturnKey
   lda GeosFileLastJoy
   and #$0f
   cmp #$0f
   beq GeosFileNoInput
   lda #ChrCRSRRight
   rts
GeosFileMouse:
   lda MouseActive
   beq GeosFileNoInput
   php
   sei
   lda MouseLogicalX
   sta MouseFrameX
   lda MouseLogicalY
   sta MouseFrameY
   lda MouseClickEdge
   pha
   lda #0
   sta MouseClickEdge
   pla
   plp
   pha
   lda #1
   sta MouseMenuEnabled
   jsr Mouse1351ShowPointer
   pla
   beq GeosFileNoInput
   lda MouseFrameY
   cmp #144
   bcc GeosFileNoInput
   cmp #160
   bcs GeosFileNoInput
   lda MouseFrameX          ;half-pixel X: 4 units per text column
   cmp #20
   bcc GeosFileNoInput
   cmp #60
   bcc GeosFileMouseCancel
   ldx GeosFileLastState
   cpx #rfosDeleteReady
   bne GeosFileNoInput
   cmp #96
   bcc GeosFileNoInput
   cmp #136
   bcs GeosFileNoInput
   lda #1
   bne +
GeosFileMouseCancel:
   lda #0
+  sta GeosFileChoice
GeosFileReturnKey:
   lda #ChrReturn
   rts
GeosFileNoInput:
   lda #0
   rts

; Draw directly into the bitmap. All 255 raw ASCII name
; bytes fit in seven rows of 38 characters; controls cannot escape the panel.
GeosFileDraw:
   jsr Mouse1351HideForRedraw
   lda #0
   sta GeosBitmapReverse
   lda #GeosBitmapColorNormal
   sta GeosBitmapColor
   lda #4
   sta GeosFileRow
-  ldx GeosFileRow
   jsr GeosBitmapBlankLine
   inc GeosFileRow
   lda GeosFileRow
   cmp #21
   bne -
   ldx #4
   ldy #1
   jsr GeosBitmapSetCursor
   lda #<GeosFileTitle
   ldy #>GeosFileTitle
   jsr GeosBitmapPrintString
   lda #6
   sta GeosFileRow
   ldx #6
   ldy #1
   jsr GeosBitmapSetCursor
   lda #38
   sta GeosFileColumn
   lda #rsstFileOpName
   sta rwRegSerialString+IO1Port
GeosFileNameLoop:
   lda rwRegSerialString+IO1Port
   beq GeosFileMessage
   cmp #32
   bcc +
   cmp #127
   bcc ++
+
   lda #'?'
++ jsr GeosFilePutNameChar
   dec GeosFileColumn
   bne GeosFileNameLoop
   inc GeosFileRow
   ldx GeosFileRow
   cpx #13
   bcs GeosFileNameDrain
   ldy #1
   jsr GeosBitmapSetCursor
   lda #38
   sta GeosFileColumn
   bne GeosFileNameLoop
GeosFileNameDrain:
   lda rwRegSerialString+IO1Port
   bne GeosFileNameDrain
GeosFileMessage:
   ldx #14
   ldy #0
   jsr GeosBitmapSetCursor
   lda #rsstFileOpMessage
   ldx #39
   jsr GeosBitmapPrintSerialLimited
   lda #$ff
   sta GeosFileLastProgress
GeosFileButtons:
   lda #0
   sta GeosBitmapReverse
   ldx #18
   jsr GeosBitmapBlankLine
   ldx #18
   ldy #5
   jsr GeosBitmapSetCursor
   lda GeosFileChoice
   eor #1
   sta GeosBitmapReverse
   lda GeosFileLastState
   cmp #rfosBusy
   beq +
   cmp #rfosDeleteReady
   beq +
   lda #<GeosFileOK
   ldy #>GeosFileOK
   bne ++
+  lda #<GeosFileCancelText
   ldy #>GeosFileCancelText
++ jsr GeosBitmapPrintString
   lda GeosFileLastState
   cmp #rfosDeleteReady
   bne GeosFileButtonsDone
   ldx #18
   ldy #24
   jsr GeosBitmapSetCursor
   lda GeosFileChoice
   sta GeosBitmapReverse
   lda #<GeosFileDeleteText
   ldy #>GeosFileDeleteText
   jsr GeosBitmapPrintString
GeosFileButtonsDone:
   lda #0
   sta GeosBitmapReverse
   rts

; File names use raw ASCII art, never the PETSCII graphics slots overwritten
; by browser icons. Share only the bitmap glyph/color/cursor output tail.
GeosFilePutNameChar:
   php
   sei
   sec
   sbc #32
   ldx #0
   stx GeosBitmapFontOffsetHi
   asl
   rol GeosBitmapFontOffsetHi
   asl
   rol GeosBitmapFontOffsetHi
   asl
   rol GeosBitmapFontOffsetHi
   clc
   adc #<GeosRichFont
   sta PtrAddrLo
   lda GeosBitmapFontOffsetHi
   adc #>GeosRichFont
   sta PtrAddrHi
   jsr GeosBitmapSetCellPointer
   ldy #0
   jmp GeosBitmapPutGlyphLoop

GeosFileTitle: !tx "FILE OPERATIONS",0
GeosFileCancelText: !tx "[ CANCEL ]",0
GeosFileDeleteText: !tx "[ DELETE ]",0
GeosFileOK: !tx "[   OK   ]",0
GeosFileLastState: !byte 0
GeosFileLastProgress: !byte 0
GeosFileLastJoy: !byte $ff
GeosFileChoice: !byte 0
GeosFileRow: !byte 0
GeosFileColumn: !byte 0
