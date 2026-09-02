; Resident desktop demos. Separate payload: never load a PRG over the desktop.
!convtab pet
!ifdef PreviewApps {
   !src "build/vice-preview/DesktopSymbols"
} else {
   !src "build/DesktopSymbols"
}
* = $c000
   jmp AppEnter
AppBackendAvailable: !byte 1       ;VICE sets this to zero; no fake file service

   jmp ViewTextFileImpl          ;$c004: classic text viewer
   jmp ShowSIDAdvancedImpl       ;$c007: detailed SID controls
   jmp AppPublishControlLabel    ;$c00a: bounded live control-label publication
   jmp AppSelectHome            ;$c00d: live home labels and footer

AppEnter:
   cld
   sta AppID
   lda #0
   sta AppExit
   sta AppFrameReady
   sta GeosBitmapActive
   sta MouseClickEdge
   sta MouseOpenArmed
   lda #$ff
   sta AppJoyLast
   lda AppID
   beq AppInitSnake
   cmp #1
   beq AppInitCalc
   jsr TextInit
   jmp AppRedraw
AppInitSnake:
   jsr SnakeInit
   jmp AppRedraw
AppInitCalc:
   jsr CalcInit
AppRedraw:
   lda AppDirty
   sta AppRenderMode
   lda #0
   sta AppDirty
   jsr AppBegin
   lda AppID
   beq AppDrawSnake
   cmp #1
   beq AppDrawCalc
   jsr TextDraw
   jmp AppDrawDone
AppDrawSnake:
   jsr SnakeDraw
   jmp AppDrawDone
AppDrawCalc:
   jsr CalcDraw
AppDrawDone:
   jsr GeosRichPublish
   jsr GeosBitmapPublishColors
   lda RichSavedBank
   sta $01
AppLoop:
   jsr GeosRichClock
   lda $a2
   sta AppTick
   lda AppID
   bne +
   jsr SnakeTick
+  jsr GetIn
   beq AppJoystick
   jsr AppKey
AppJoystick:
   lda Joystick2Sample
   cmp AppJoyLast
   beq AppMouse
   sta AppJoyLast
   ldx #0
-  lsr
   bcc +
   inx
   cpx #5
   bne -
   beq AppMouse
+  lda AppJoyKeys,x
   jsr AppKey
AppMouse:
   php
   sei
   lda MouseLogicalX
   sta MouseFrameX
   lda MouseLogicalY
   sta MouseFrameY
   lda MouseClickEdge
   sta AppClick
   lda #0
   sta MouseClickEdge
   plp
   lda MouseActive
   beq AppCheckDone
   lda #1
   sta MouseMenuEnabled
   jsr Mouse1351ShowPointer
   lda AppClick
   beq AppCheckDone
   lda MouseFrameX
   lsr
   lsr
   tax
   lda MouseFrameY
   lsr
   lsr
   lsr
   tay
   cpx #36
   bcc AppBodyClick
   cpx #39
   bcs AppBodyClick
   cpy #1
   bcc AppBodyClick
   cpy #4
   bcc AppClose
AppBodyClick:
   lda AppID
   beq AppClickSnake
   cmp #1
   beq AppClickCalc
   jsr TextClick
   jmp AppCheckDone
AppClickSnake:
   jsr SnakeClick
   jmp AppCheckDone
AppClickCalc:
   jsr CalcClick
AppCheckDone:
   lda AppExit
   bne AppReturn
   lda AppDirty
   bne +
   jmp AppLoop
+
   jmp AppRedraw
AppClose:
   lda #1
   sta AppExit
AppReturn:
   lda #0
   sta MouseClickEdge
   sta MouseOpenArmed
   sta GeosMouseWasDown
   sta GeosDragActive
   lda #$ff
   sta GeosDragCandidate
   lda AppExit
   rts
AppKey:
   cmp #ChrStop
   beq AppCloseKey
   cmp #27
   beq AppCloseKey
   cmp #ChrHome
   beq AppCloseKey
   ldx AppID
   beq +
   cpx #1
   beq ++
   jmp TextKey
+  jmp SnakeKey
++ jmp CalcKey
AppCloseKey:
   lda #1
   sta AppExit
   rts

; A framed monochrome app window over the desktop. All drawing stays offscreen.
AppBegin:
   jsr GeosRichBegin
   lda #>(GeosLayoutScreen-C64ScreenRAM)
   sta GeosBitmapColorOffset
   lda AppFrameReady
   beq AppCreateWindow
   lda AppRenderMode
   cmp #2
   bne +
   rts
+
   jmp AppClearInterior
AppCreateWindow:
   inc AppFrameReady
   jsr GeosRichHome
   lda #8
   sta RichX
   lda #12
   sta RichY
   lda #48
   sta RichW
   lda #1
   sta RichWHi
   lda #0
   sta RichXHi
   lda #176
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   lda #9
   sta RichX
   lda #13
   sta RichY
   lda #46
   sta RichW
   lda #174
   sta RichH
   lda #0
   sta RichInk
   jsr RichRect
   lda #28
   sta RichY
   lda #1
   sta RichH
   lda #$ff
   sta RichInk
   jsr RichRect
   ldx #20
   ldy #18
   jsr AppPosition
   ldx AppID
   cpx #3
   bcc +
   ldx #2
+  lda AppTitleLo,x
   ldy AppTitleHi,x
   jsr RichText
   lda #1
   sta RichXHi
   lda #38
   sta RichX
   lda #15
   sta RichY
   lda #12
   sta RichW
   sta RichH
   lda #0
   sta RichWHi
   jsr RichRect
   lda #39
   sta RichX
   lda #16
   sta RichY
   lda #10
   sta RichW
   sta RichH
   lda #0
   sta RichInk
   jsr RichRect
   lda #41
   sta RichX
   lda #18
   sta RichY
   lda #$ff
   sta RichInk
   lda #'X'
   jmp RichChar

; Interior is 36 whole bitmap cells across, rows4..22. Clear straight bytes,
; not thousands of individual pixel operations; keep window chrome intact.
AppClearInterior:
   lda #$10
   sta AppClearPage+1
   sta AppClearTail+1
   lda #$a5
   sta AppClearPage+2
   lda #$a6
   sta AppClearTail+2
   ldx #19
AppClearRow:
   lda #0
   ldy #0
AppClearPage:
   sta $a510,y
   iny
   bne AppClearPage
   ldy #31
AppClearTail:
   sta $a610,y
   dey
   bpl AppClearTail
   lda AppClearPage+1
   clc
   adc #$40
   sta AppClearPage+1
   sta AppClearTail+1
   lda AppClearPage+2
   adc #1
   sta AppClearPage+2
   clc
   adc #1
   sta AppClearTail+2
   dex
   bne AppClearRow
   rts

AppPosition:
   stx RichX
   sty RichY
   lda #0
   sta RichXHi
   lda #$ff
   sta RichInk
   rts

; Unsigned decimal; caller handles a sign if needed. AppNumber is consumed.
AppPrintNumber:
   lda #0
   sta AppNumIndex
   sta AppNumLeading
AppNumberDigit:
   lda #'0'
   sta AppNumDigit
   ldx AppNumIndex
AppNumberSubtract:
   lda AppNumber
   sec
   sbc AppPowersLo,x
   tay
   lda AppNumber+1
   sbc AppPowersHi,x
   bcc AppNumberEmit
   sta AppNumber+1
   sty AppNumber
   inc AppNumDigit
   bne AppNumberSubtract
AppNumberEmit:
   lda AppNumDigit
   cmp #'0'
   bne +
   lda AppNumLeading
   bne +
   cpx #4
   bne AppNumberNext
+  lda #1
   sta AppNumLeading
   lda AppNumDigit
   jsr RichChar
AppNumberNext:
   inc AppNumIndex
   lda AppNumIndex
   cmp #5
   bne AppNumberDigit
   rts

AppJoyKeys: !byte ChrCRSRUp,ChrCRSRDn,ChrCRSRLeft,ChrCRSRRight,ChrReturn
AppTitleLo: !byte <AppTitleSnake,<AppTitleCalc,<AppTitleText
AppTitleHi: !byte >AppTitleSnake,>AppTitleCalc,>AppTitleText
AppTitleSnake: !tx "SNAKE",0
AppTitleCalc: !tx "CALCULATOR",0
AppTitleText: !tx "TEXT VIEWER",0
AppPowersLo: !byte <10000,<1000,<100,<10,<1
AppPowersHi: !byte >10000,>1000,>100,>10,>1
AppID: !byte 0
AppExit: !byte 0
AppDirty: !byte 0
AppFrameReady: !byte 0
AppRenderMode: !byte 0
AppClick: !byte 0
AppJoyLast: !byte $ff
AppTick: !byte 0
AppNumber: !word 0
AppNumIndex: !byte 0
AppNumDigit: !byte 0
AppNumLeading: !byte 0

!src "source/GeosAppSnake.s"
!src "source/GeosAppCalculator.s"
!src "source/GeosAppText.s"
!src "source/LegacyTextViewer.s"
!src "source/LegacySIDInfo.s"

; RichItem identifies an already composed 72x9 control label. The caller has
; exposed native RAM under BASIC and masked IRQs. Copy its two containing
; character rows only; colors, pointer, other labels and footer stay untouched.
AppPublishControlLabel:
   jsr GeosControlOrigin
   lda RichX
   sec
   sbc #24
   sta RichX
   lda RichY
   clc
   adc #19
   and #$f8
   sta RichY
   lda #2
   sta AppPublishLabelRows
AppPublishLabelRow:
   jsr RichAddress
   lda RichRead+1
   sta AppPublishLabelRead+1
   sta AppPublishLabelWrite+1
   lda RichRead+2
   sta AppPublishLabelRead+2
   sec
   sbc #$80
   sta AppPublishLabelWrite+2
   ldy #71
AppPublishLabelRead:
   lda $ffff,y
AppPublishLabelWrite:
   sta $ffff,y
   dey
   bpl AppPublishLabelRead
   lda RichY
   clc
   adc #8
   sta RichY
   dec AppPublishLabelRows
   bne AppPublishLabelRow
   rts
AppPublishLabelRows: !byte 0

; A=new home icon, different from the selected icon. Reuse the authored label
; renderer and footer with live pixel mirroring; never reinstall the charset,
; touch icon artwork or compare a whole frame. Restore mirror/bank/IRQ state.
AppSelectHome:
   ldx GeosHomeSelection
   stx RichItem
   sta GeosHomeSelection
   php
   sei
   jsr GeosRichBegin
   lda #$ea                    ;NOP: continue through the native mirror store
   sta RichMirrorMode
   jsr RichHomeOrigin
   jsr RichHomeLabelStart
   lda GeosHomeSelection
   sta RichItem
   jsr RichHomeOrigin
   jsr RichHomeLabelStart
   jsr RichClearFooter
   jsr RichHomeFooter
   lda #$60                    ;RTS: ordinary drawing is staged again
   sta RichMirrorMode
   lda RichSavedBank
   sta $01
   plp
   rts

GeosAppsEnd:
!if GeosAppsEnd > $d000 {
   !error "Desktop apps exceed reserved $c000-$cfff RAM"
}
