; 1351 proportional mouse support for the GEOS-style main-menu desktop.
;
; The mouse is read from control port 1.  Keyboard scanning and joystick 2
; remain owned by the existing KERNAL/main-loop paths.  The IRQ sampler reads
; the SID POT counters before the KERNAL keyboard scan.  While a port-1 switch
; is held, the sampler temporarily blinds SCNKEY using the established 1351
; technique, preventing a mouse click from appearing as phantom keyboard input.

   MouseLogicalXMax = 159
   MouseLogicalYMax = 199
   MouseScreenXBias = 24
   MouseScreenYBias = 50
   ;Aligned tape-buffer RAM is free while the menu is active.  The PRG launch
   ;stub may overwrite it only after RunSelected has hidden the pointer.
   MouseSpriteDataRAM = $0340
   MouseSpritePointerValue = MouseSpriteDataRAM/64
   MouseMotionActivateFrames = 3
   MouseClickActivateFrames = 2
   MousePlausibleDeltaLimit = 17 ;absolute deltas 1..16 count as presence

   ;Private virtual-key values consumed before the normal keyboard map.
   MouseEventPagePrev = $f0
   MouseEventPageNext = $f1

; Copy the sprite, reset the input state, and leave the pointer hidden.  Port 1
; is the KERNAL's normal idle POT selection, so the CIA data latches are not
; changed here; this also avoids disturbing a joystick held in port 2.
Mouse1351Init:
   php
   sei
   lda #0
   sta MouseCalibrated
   sta MouseActive
   sta MouseMenuEnabled
   sta MouseMotionScore
   sta MouseButtonScore
   sta MouseFrameMoved
   sta MouseLeftDown
   sta MouseNewLeftDown
   sta MouseClickEdge
   sta MouseOpenArmed
   lda #80
   sta MouseLogicalX
   lda #100
   sta MouseLogicalY

   ldx #63
MouseCopySprite:
   lda MousePointerSpriteData,x
   sta MouseSpriteDataRAM,x
   dex
   bpl MouseCopySprite

   lda #MouseSpritePointerValue
   sta Sprite0Pointer
   lda #PokeBlack
   sta Sprite0Color
   lda SpriteEnable
   and #%11111110
   sta SpriteEnable
   lda SpriteYExpand
   and #%11111110
   sta SpriteYExpand
   lda SpriteXExpand
   and #%11111110
   sta SpriteXExpand
   lda SpriteMulticolor
   and #%11111110
   sta SpriteMulticolor
   lda SpritePriority
   and #%11111110
   sta SpritePriority
   plp
   rts

; Disable only sprite 0 and discard menu-local click state.  Mouse presence and
; coordinates survive a viewer/modal so the pointer can return on the desktop.
Mouse1351Hide:
   lda SpriteEnable
   and #%11111110
   sta SpriteEnable
   lda #0
   sta MouseMenuEnabled
   sta MouseClickEdge
   sta MouseOpenArmed
   rts

; Called once around the main menu loop.  Carry clear means no virtual key;
; carry set returns an existing key code (or a private page event) in A.
Mouse1351ProcessMenu:
   lda GeosViewMode
   bne MouseProcessCheckActive
   jsr Mouse1351Hide
   clc
   rts

MouseProcessCheckActive:
   lda MouseActive
   bne MouseProcessSnapshot
   jsr Mouse1351Hide
   clc
   rts

MouseProcessSnapshot:
   lda #1
   sta MouseMenuEnabled
   php
   sei
   lda MouseLogicalX
   sta MouseFrameX
   lda MouseLogicalY
   sta MouseFrameY
   lda MouseClickEdge
   sta MouseFrameClick
   lda #0
   sta MouseClickEdge
   plp

   jsr Mouse1351ShowPointer

   ;A keyboard/joystick move changes the shared cursor item.  Do not let a
   ;later mouse click count as a second click on stale mouse state.
   lda MouseOpenArmed
   beq MouseProcessClickCheck
   lda MouseLastClickedItem
   cmp rwRegCursorItemOnPg+IO1Port
   beq MouseProcessClickCheck
   lda #0
   sta MouseOpenArmed

MouseProcessClickCheck:
   lda MouseFrameClick
   bne MouseProcessClick
   clc
   rts

MouseProcessClick:
   lda MouseFrameX
   lsr
   lsr
   tax                         ;logical X / 4 = text column
   lda MouseFrameY
   lsr
   lsr
   lsr
   tay                         ;logical Y / 8 = text row

   cpy #0
   beq MouseHitTitleBar
   cpy #1
   beq MouseHitPageBar
   cpy #22
   beq MouseHitSourceBar
   cpy #24
   beq MouseHitActionBar
   jmp MouseHitDesktop

; The visible V VIEW label is clickable.  F2/BASIC intentionally remains a
; keyboard-only operation so an imprecise pointer cannot exit unexpectedly.
MouseHitTitleBar:
   cpx #19
   bcs +
   jmp MouseNoTarget
+
   cpx #27
   bcc +
   jmp MouseNoTarget
+
   lda #'V'
   jmp MouseReturnVirtualKey

; The left and right halves of the visible Pg n/m field page backward/forward.
MouseHitPageBar:
   cpx #18
   bcs +
   jmp MouseNoTarget
+
   cpx #24
   bcc MouseReturnPagePrev
   cpx #29                     ;stop before the clock at column 29
   bcc +
   jmp MouseNoTarget
+
   lda #MouseEventPageNext
   jmp MouseReturnVirtualKey
MouseReturnPagePrev:
   lda #MouseEventPagePrev
   jmp MouseReturnVirtualKey

; Footer row 22 contains the existing source/help labels.
MouseHitSourceBar:
   cpx #10
   bcc MouseReturnF1
   cpx #17
   bcc MouseReturnF3
   cpx #25
   bcc MouseReturnF5
   cpx #35
   bcc MouseReturnF7
   jmp MouseNoTarget
MouseReturnF1:
   lda #ChrF1
   jmp MouseReturnVirtualKey
MouseReturnF3:
   lda #ChrF3
   jmp MouseReturnVirtualKey
MouseReturnF5:
   lda #ChrF5
   jmp MouseReturnVirtualKey
MouseReturnF7:
   lda #ChrF7
   jmp MouseReturnVirtualKey

; Footer row 24 exposes Parent, Home, Music, and Settings with their existing
; keyboard codes.  This keeps the shortcut implementation single-sourced.
MouseHitActionBar:
   cpx #9
   bcc MouseReturnParent
   cpx #19
   bcc MouseReturnHome
   cpx #29
   bcc MouseReturnF4
   cpx #38
   bcc MouseReturnF8
   jmp MouseNoTarget
MouseReturnParent:
   lda #ChrUpArrow
   jmp MouseReturnVirtualKey
MouseReturnHome:
   lda #ChrHome
   jmp MouseReturnVirtualKey
MouseReturnF4:
   lda #ChrF4
   jmp MouseReturnVirtualKey
MouseReturnF8:
   lda #ChrF8
   jmp MouseReturnVirtualKey

MouseHitDesktop:
   jsr GeosHitTest
   bcc MouseNoTarget
   sta MouseHitItem

   ;Opening is deliberately two-click, not merely "already keyboard focused".
   ;The first click selects/arms an item; a later click on that same still-
   ;selected item returns Return through the existing SelectItem path.
   lda MouseOpenArmed
   beq MouseSelectAndArm
   lda MouseLastClickedItem
   cmp MouseHitItem
   bne MouseSelectAndArm
   lda rwRegCursorItemOnPg+IO1Port
   cmp MouseHitItem
   bne MouseSelectAndArm
   lda #0
   sta MouseOpenArmed
   lda #ChrReturn
   sec
   rts

MouseSelectAndArm:
   lda MouseHitItem
   sta MouseLastClickedItem
   lda #1
   sta MouseOpenArmed
   lda MouseHitItem
   jsr GeosSetSelection
   clc
   rts

MouseNoTarget:
   lda #0
   sta MouseOpenArmed
   clc
   rts

MouseReturnVirtualKey:
   pha
   lda #0
   sta MouseOpenArmed
   pla
   sec
   rts

; Convert the 160x200 logical position to VIC sprite coordinates.  The screen
; origin biases keep the pointer hotspot aligned with the 40x25 text display.
Mouse1351ShowPointer:
   lda #MouseSpritePointerValue
   sta Sprite0Pointer
   lda #1
   sta Sprite0Color

   lda MouseFrameX
   asl
   sta MousePhysicalXLo
   lda #0
   adc #0
   sta MousePhysicalXHi
   lda MousePhysicalXLo
   clc
   adc #MouseScreenXBias
   sta Sprite0Xpos
   lda MousePhysicalXHi
   adc #0
   and #1
   sta MousePhysicalXHi
   lda SpriteXMSB
   and #%11111110
   ora MousePhysicalXHi
   sta SpriteXMSB

   lda MouseFrameY
   clc
   adc #MouseScreenYBias
   sta Sprite0Ypos

   lda SpriteYExpand
   and #%11111110
   sta SpriteYExpand
   lda SpriteXExpand
   and #%11111110
   sta SpriteXExpand
   lda SpriteMulticolor
   and #%11111110
   sta SpriteMulticolor
   lda SpritePriority
   and #%11111110
   sta SpritePriority
   lda SpriteEnable
   ora #1
   sta SpriteEnable
   rts

; IRQ-side sampler.  The first sample is calibration only.  Presence becomes
; active after three consecutive plausible movement frames or after a stable,
; deliberate port-1 fire press lasting two samples.  The activating press is
; consumed, preventing a stationary mouse from accidentally opening an item.
Mouse1351IRQSample:
   lda PadlXReg
   sta MouseNewPotX
   lda PadlYReg
   sta MouseNewPotY

   ;Match the established cc65/Commodore 1351 scan: isolate the keyboard,
   ;sample control port 1, then return port A to output.  If any port-1 line is
   ;active, make port B an all-zero output so the immediately following KERNAL
   ;keyboard scan sees an impossible all-keys condition and discards it.  On
   ;release DDRB remains input, the normal SCNKEY state.
   lda #0
   sta CIA1_DDRB
   sta CIA1_DDRA
   lda CIA1_RegB
   sta MousePort1Sample
   dec CIA1_DDRA
   cmp #$ff
   beq MousePort1NotActive
   dec CIA1_DDRB
   lda #0
   sta CIA1_RegB
MousePort1NotActive:

   lda MousePort1Sample
   and #%00010000
   bne MouseButtonReleasedSample
   lda #1
   bne MouseStoreNewButton
MouseButtonReleasedSample:
   lda #0
MouseStoreNewButton:
   sta MouseNewLeftDown

   lda MouseCalibrated
   bne MouseSampleMovement
   lda MouseNewPotX
   sta MouseOldPotX
   lda MouseNewPotY
   sta MouseOldPotY
   lda MouseNewLeftDown
   sta MouseLeftDown
   lda #1
   sta MouseCalibrated
   rts

MouseSampleMovement:
   lda #0
   sta MouseFrameMoved

   lda MouseNewPotX
   ldy MouseOldPotX
   jsr Mouse1351DecodeDelta
   bcc MouseSampleY
   sty MouseOldPotX
   sta MouseDelta
   jsr Mouse1351ApplyX
   jsr Mouse1351MarkPlausibleMovement

MouseSampleY:
   lda MouseNewPotY
   ldy MouseOldPotY
   jsr Mouse1351DecodeDelta
   bcc MouseUpdatePresence
   sty MouseOldPotY
   sta MouseDelta
   jsr Mouse1351ApplyY
   jsr Mouse1351MarkPlausibleMovement

MouseUpdatePresence:
   lda MouseActive
   bne MouseActiveButtonEdge

   lda MouseFrameMoved
   beq MouseResetMotionScore
   inc MouseMotionScore
   lda MouseMotionScore
   cmp #MouseMotionActivateFrames
   bcc MouseCheckActivationClick
   lda #1
   sta MouseActive
   lda #0
   sta MouseMotionScore
   sta MouseButtonScore
   jmp MouseStoreButtonState

MouseResetMotionScore:
   lda #0
   sta MouseMotionScore

MouseCheckActivationClick:
   lda MouseNewLeftDown
   beq MouseResetButtonScore
   inc MouseButtonScore
   lda MouseButtonScore
   cmp #MouseClickActivateFrames
   bcc MouseStoreButtonState
   lda #1
   sta MouseActive
   lda #0
   sta MouseMotionScore
   sta MouseButtonScore
   jmp MouseStoreButtonState

MouseResetButtonScore:
   lda #0
   sta MouseButtonScore
   jmp MouseStoreButtonState

MouseActiveButtonEdge:
   lda MouseNewLeftDown
   beq MouseStoreButtonState
   lda MouseLeftDown
   bne MouseStoreButtonState
   lda MouseMenuEnabled
   beq MouseStoreButtonState
   lda #1
   sta MouseClickEdge

MouseStoreButtonState:
   lda MouseNewLeftDown
   sta MouseLeftDown
   rts

; A=current POT value, Y=last accepted value.  Carry set returns a signed
; logical delta in A and the new baseline in Y.  Carry clear rejects no motion
; and the +/- one-count SID noise bit, retaining the previous baseline.
Mouse1351DecodeDelta:
   sty MouseOldValue
   sta MouseNewValue
   sec
   sbc MouseOldValue
   and #%01111111
   cmp #%01000000
   bcs MouseDecodeNegative
   lsr
   beq MouseDecodeRejected
   ldy MouseNewValue
   sec
   rts

MouseDecodeNegative:
   ora #%11000000
   cmp #$ff
   beq MouseDecodeRejected
   sec
   ror
   ldy MouseNewValue
   sec
   rts

MouseDecodeRejected:
   lda #0
   clc
   rts

Mouse1351ApplyX:
   lda MouseDelta
   bmi MouseApplyXNegative
   clc
   adc MouseLogicalX
   cmp #MouseLogicalXMax+1
   bcc MouseStoreX
   lda #MouseLogicalXMax
MouseStoreX:
   sta MouseLogicalX
   rts
MouseApplyXNegative:
   clc
   adc MouseLogicalX
   bcs MouseStoreX
   lda #0
   sta MouseLogicalX
   rts

; The 1351 Y counter grows upward, opposite the screen coordinate direction.
Mouse1351ApplyY:
   lda MouseDelta
   eor #$ff
   clc
   adc #1
   bmi MouseApplyYNegative
   clc
   adc MouseLogicalY
   cmp #MouseLogicalYMax+1
   bcc MouseStoreY
   lda #MouseLogicalYMax
MouseStoreY:
   sta MouseLogicalY
   rts
MouseApplyYNegative:
   clc
   adc MouseLogicalY
   bcs MouseStoreY
   lda #0
   sta MouseLogicalY
   rts

Mouse1351MarkPlausibleMovement:
   lda MouseDelta
   bpl MouseMovementAbsoluteReady
   eor #$ff
   clc
   adc #1
MouseMovementAbsoluteReady:
   cmp #MousePlausibleDeltaLimit
   bcs MouseMovementMarkDone
   lda #1
   sta MouseFrameMoved
MouseMovementMarkDone:
   rts

; One-color GEOS-style arrow, copied to aligned VIC-bank-0 tape-buffer RAM.
MousePointerSpriteData:
   !byte %10000000,0,0
   !byte %11000000,0,0
   !byte %11100000,0,0
   !byte %11110000,0,0
   !byte %11111000,0,0
   !byte %11111100,0,0
   !byte %11111110,0,0
   !byte %11111111,0,0
   !byte %11111100,0,0
   !byte %11001100,0,0
   !byte %10000110,0,0
   !byte %00000110,0,0
   !byte %00000011,0,0
   !byte %00000011,0,0
   !byte 0,0,0
   !byte 0,0,0
   !byte 0,0,0
   !byte 0,0,0
   !byte 0,0,0
   !byte 0,0,0
   !byte 0,0,0
   !byte 0

MouseCalibrated:       !byte 0
MouseActive:           !byte 0
MouseMenuEnabled:      !byte 0
MouseMotionScore:      !byte 0
MouseButtonScore:      !byte 0
MouseFrameMoved:       !byte 0
MouseLogicalX:         !byte 80
MouseLogicalY:         !byte 100
MouseOldPotX:          !byte 0
MouseOldPotY:          !byte 0
MouseNewPotX:          !byte 0
MouseNewPotY:          !byte 0
MouseOldValue:         !byte 0
MouseNewValue:         !byte 0
MouseDelta:            !byte 0
MousePort1Sample:      !byte $ff
MouseNewLeftDown:      !byte 0
MouseLeftDown:         !byte 0
MouseClickEdge:        !byte 0
MouseFrameClick:       !byte 0
MouseFrameX:           !byte 80
MouseFrameY:           !byte 100
MousePhysicalXLo:      !byte 0
MousePhysicalXHi:      !byte 0
MouseOpenArmed:        !byte 0
MouseLastClickedItem:  !byte 0
MouseHitItem:          !byte 0
