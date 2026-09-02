; Bitmap text viewer: existing Teensy text stream, read-only, paged both ways.
TextInit:
   lda #0
   sta TextPage
   sta TextMore
   rts
TextKey:
   cmp #ChrCRSRLeft
   beq TextPrevious
   cmp #ChrCRSRUp
   beq TextPrevious
   cmp #ChrReturn
   beq TextNext
   cmp #ChrSpace
   beq TextNext
   cmp #ChrCRSRRight
   beq TextNext
   cmp #ChrCRSRDn
   beq TextNext
   and #$7f
   ora #$20
   cmp #$6f                  ;normalized ASCII o, independent of !convtab pet
   bne TextDone
TextOpen:
   lda AppBackendAvailable
   beq TextDone
   lda #2
   sta AppExit
TextDone:
   rts
TextPrevious:
   lda TextPage
   beq TextDone
   dec TextPage
   jmp TextChanged
TextNext:
   lda TextMore
   beq TextDone
   lda TextPage
   cmp #254
   beq TextDone
   inc TextPage
TextChanged:
   lda #1
   sta AppDirty
   rts
TextClick:
   cpy #22
   bne TextDone
   cpx #2
   bcc TextDone
   cpx #8
   bcc TextOpen
   cpx #12
   bcc TextDone
   cpx #18
   bcc TextPrevious
   cpx #31
   bcc TextDone
   cpx #37
   bcc TextNext
   rts

TextDraw:
   lda #<TextWelcome
   sta TextReadDemo+1
   lda #>TextWelcome
   sta TextReadDemo+2
   lda AppID
   cmp #3
   bne TextReadStart
   ; Reloading the selected read-only stream makes previous-page navigation
   ; deterministic without a large C64-side file cache. Never call CHROUT.
   lda #rCtlStartSelItemWAIT
   sta wRegControl+IO1Port
TextWait:
   lda rwRegStatus+IO1Port
   cmp #rsReady
   beq TextReadStart
   cmp #rsC64Message
   bne TextWait
   lda #rsstSerialStringBuf
   sta rwRegSerialString+IO1Port
-  lda rwRegSerialString+IO1Port
   bne -
   lda #rsContinue
   sta rwRegStatus+IO1Port
   jmp TextWait
TextReadStart:
   lda TextPage
   sta TextSkip
   lda #0
   sta TextLastCR
   sta TextWrapped
TextStartPage:
   lda #0
   sta TextRow
   sta TextColumn
   ldx #16
   ldy #36
   jsr AppPosition
TextReadLoop:
   jsr TextGetByte
   bcs TextHaveByte
   jmp TextEndFile
TextHaveByte:
   cmp #13
   beq TextCarriageReturn
   cmp #10
   bne +
   ldx TextLastCR
   beq TextLineFeed
   lda #0
   sta TextLastCR
   jmp TextReadLoop
TextLineFeed:
   ldx TextWrapped
   beq TextNewLine
   lda #0
   sta TextWrapped
   jmp TextReadLoop
+  ldx #0
   stx TextLastCR
   stx TextWrapped
   and #$7f
   cmp #32
   bcc TextReadLoop           ;do not execute PETSCII color/screen controls
   sta TextCharacter
   lda TextSkip
   bne +
   lda TextCharacter
   jsr RichChar
+  inc TextColumn
   lda TextColumn
   cmp #48
   bne TextReadLoop
   lda #1
   sta TextWrapped
   bne TextNewLine
TextCarriageReturn:
   lda #1
   sta TextLastCR
   lda TextWrapped
   beq TextNewLine
   lda #0
   sta TextWrapped
   jmp TextReadLoop
TextNewLine:
   lda #0
   sta TextColumn
   inc TextRow
   lda TextRow
   cmp #17
   beq TextEndPage
   asl
   asl
   asl
   clc
   adc #36
   tay
   ldx #16
   jsr AppPosition
   jmp TextReadLoop
TextEndPage:
   lda TextSkip
   beq +
   dec TextSkip
   jmp TextStartPage
+  jsr TextAvailable
   sta TextMore
   jmp TextFooter
TextEndFile:
   lda #0
   sta TextMore
TextFooter:
   ldx #16
   jsr TextButton
   ldx #20
   ldy #177
   jsr AppPosition
   lda #<TextOpenLabel
   ldy #>TextOpenLabel
   jsr RichText
   ldx #96
   jsr TextButton
   ldx #100
   ldy #177
   jsr AppPosition
   lda #<TextPrevLabel
   ldy #>TextPrevLabel
   jsr RichText
   ldx #248
   jsr TextButton
   ldx #252
   ldy #177
   jsr AppPosition
   lda #<TextNextLabel
   ldy #>TextNextLabel
   jsr RichText
   ldx #180
   ldy #177
   jsr AppPosition
   lda TextPage
   clc
   adc #1
   sta AppNumber
   lda #0
   sta AppNumber+1
   jmp AppPrintNumber
TextButton:
   ldy #175
   jsr AppPosition
   lda #48
   sta RichW
   lda #0
   sta RichWHi
   lda #11
   sta RichH
   jsr RichRect
   inc RichX
   inc RichY
   lda #46
   sta RichW
   lda #9
   sta RichH
   lda #0
   sta RichInk
   jmp RichRect

TextAvailable:
   lda AppID
   cmp #3
   bne +
   lda rRegStrAvailable+IO1Port
   rts
+  lda TextReadDemo+1
   sta TextPeekDemo+1
   lda TextReadDemo+2
   sta TextPeekDemo+2
TextPeekDemo:
   lda TextWelcome
   rts
TextGetByte:
   jsr TextAvailable
   beq TextNoByte
   lda AppID
   cmp #3
   bne TextReadDemo
   lda rRegStreamData+IO1Port
   sec
   rts
TextReadDemo:
   lda TextWelcome
   inc TextReadDemo+1
   bne +
   inc TextReadDemo+2
+  sec
   rts
TextNoByte:
   clc
   rts

TextOpenLabel: !tx "OPEN",0
TextPrevLabel: !tx "< PREV",0
TextNextLabel: !tx "NEXT >",0
TextWelcome:
   !tx "TEENSYROM DESKTOP TOOLS",13,13
   !tx "A real 320x200 bitmap text viewer.",13,13
   !tx "Choose OPEN, then a TXT, NFO, MD or SEQ",13
   !tx "file in Teensy memory, SD or USB.",13,13
   !tx "Use PREV/NEXT or cursor keys to turn",13
   !tx "pages. STOP or the close button returns",13
   !tx "to the desktop. Files are never modified.",0
TextPage: !byte 0
TextMore: !byte 0
TextSkip: !byte 0
TextRow: !byte 0
TextColumn: !byte 0
TextCharacter: !byte 0
TextLastCR: !byte 0
TextWrapped: !byte 0
