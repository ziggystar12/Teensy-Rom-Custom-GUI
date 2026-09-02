; UI-only VICE preview. This embeds the real DesktopShell renderer unchanged,
; but deliberately bypasses TeensyROM startup, file services and launch actions.
; Keys: 1..5 open menus, F8 opens control panel, HOME/STOP returns home, arrows
; move local selections. RETURN and storage/settings actions are intentionally
; inactive. A 1351 mouse may move the pointer, toggle top-row menus, and
; dismiss an open menu by clicking outside it; menu item actions stay disabled.

!convtab pet
!src "build/vice-preview/DesktopSymbols"

* = $0801
!word PreviewBasicEnd
!word 10
!byte $9e
!text "2061"
!byte 0
PreviewBasicEnd:
!word 0

PreviewLoader:
   sei
   cld
   lda #$36                   ;RAM under BASIC, KERNAL and I/O remain visible
   sta $01
   lda #<PreviewAppsPayload
   sta PtrAddrLo
   lda #>PreviewAppsPayload
   sta PtrAddrHi
   lda #<GeosAppEntry
   sta Ptr2AddrLo
   lda #>GeosAppEntry
   sta Ptr2AddrHi
   jsr PreviewCopyApps
   lda #0
   sta GeosAppBackendAvailable
   ;Copy backwards: the expanded source may overlap its higher destination.
   lda #<PreviewPayloadEnd
   sta PtrAddrLo
   lda #>PreviewPayloadEnd
   sta PtrAddrHi
   lda #<PreviewDestinationEnd
   sta Ptr2AddrLo
   lda #>PreviewDestinationEnd
   sta Ptr2AddrHi
   ldy #0
PreviewCopyByte:
   lda PtrAddrLo
   bne +
   dec PtrAddrHi
+  dec PtrAddrLo
   lda Ptr2AddrLo
   bne +
   dec Ptr2AddrHi
+  dec Ptr2AddrLo
   lda (PtrAddrLo),y
   sta (Ptr2AddrLo),y
   lda PtrAddrLo
   cmp #<PreviewPayload
   bne PreviewCopyByte
   lda PtrAddrHi
   cmp #>PreviewPayload
   bne PreviewCopyByte

   ;The payload already contains home/icon defaults. Never call its real Start
   ;or GeosShellInit: both expect live TeensyROM IO1 responses.
   lda #1
   sta GeosViewMode
   sta smcSIDPauseStop+1
   lda #GeosSurfaceHome
   sta GeosSurfaceMode
   lda #GeosOverlayNone
   sta GeosOverlayMode
   jsr Mouse1351Init

   ;A deterministic demo time, advanced by the emulated C64's PAL CIA TOD.
   lda $dc0e
   ora #$80
   sta $dc0e
   lda #$10
   sta TODHoursBCD
   lda #0
   sta TODMinBCD
   sta TODSecBCD
   sta TODTenthSecBCD
   sta smc24HourClockDisp+1

   ;Keep the KERNAL keyboard/jiffy IRQ; omit all Teensy and SID IRQ services.
   lda $0314
   sta PreviewChainIRQ+1
   lda $0315
   sta PreviewChainIRQ+2
   lda #<PreviewIRQ
   sta $0314
   lda #>PreviewIRQ
   sta $0315
   cli

   jsr GeosDrawDesktop
   ldx #0
   jsr PreviewRecordMode
   jsr GeosDrawDesktop
   ldx #4
   jsr PreviewRecordMode
   lda #GeosMenuFile
   jsr GeosShellOpenMenu
   ldx #8
   jsr PreviewRecordMode
!ifndef PreviewMenu {
   jsr GeosFileDesktop
}
!ifdef PreviewIEC {
   ;Opt-in actual IEC directory read, using VICE's emulated drive 8 or 9.
   ;This does not enter any TeensyROM file or launch path.
   lda #PreviewIEC
   jsr GeosIECOpenDrive
}
   ldx #12
   jsr PreviewRecordMode
   lda #1
   sta PreviewReady
!ifdef PreviewApp {
   lda #PreviewApp
   jsr GeosShellOpenApp
}

PreviewLoop:
   jsr DisplayTime
   jsr PreviewMouse
   jsr GetIn
   beq PreviewLoop
   cmp #'1'
   bcc PreviewSpecialKey
   cmp #'6'
   bcs PreviewSpecialKey
   sec
   sbc #'1'
   jsr GeosShellOpenMenu
   jmp PreviewLoop
PreviewSpecialKey:
   cmp #'6'
   bcc PreviewOtherKey
   cmp #'9'
   bcs PreviewOtherKey
   sec
   sbc #'6'
   jsr GeosShellOpenApp
   jmp PreviewLoop
PreviewOtherKey:
   cmp #ChrReturn
   bne +
   jsr PreviewActivateApp
   jmp PreviewLoop
+
   cmp #ChrF8
   bne +
   jsr GeosShellOpenControl
   jmp PreviewLoop
+  cmp #ChrHome
   beq PreviewHome
   cmp #ChrStop
   beq PreviewHome
   cmp #ChrUpArrow
   beq PreviewHome
   cmp #ChrCRSRUp
   bne +
   jsr GeosShellCursorUp
   jmp PreviewLoop
+  cmp #ChrCRSRDn
   bne +
   jsr GeosShellCursorDown
   jmp PreviewLoop
+  cmp #ChrCRSRLeft
   bne +
   jsr GeosShellCursorLeft
   jmp PreviewLoop
+  cmp #ChrCRSRRight
   bne PreviewLoop
   jsr GeosShellCursorRight
   jmp PreviewLoop
PreviewHome:
   jsr GeosFileDesktop
   jmp PreviewLoop

PreviewIRQ:
   jsr Mouse1351IRQSample
PreviewChainIRQ:
   jmp $ea31

PreviewMouse:
   lda MouseActive
   beq PreviewMouseDone
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
   lda MouseFrameClick
   beq PreviewMouseDone
   lda MouseFrameY
   lsr
   lsr
   lsr
   tay
   lda MouseFrameX
   lsr
   lsr
   tax
; Share production header toggles and menu bounds, but never activate an item
; backed by Teensy hardware. An outside click only dismisses the open menu.
PreviewMouseClick:
   cpy #0
   bne PreviewMouseDropdown
   cpx #22
   bcs PreviewMouseDismissMenu
   jmp GeosMouseMenuBar
PreviewMouseDismissMenu:
   jmp GeosMouseDismissMenu
PreviewMouseDropdown:
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne PreviewMouseDone
   ldx MouseFrameX
   ldy MouseFrameY
   jsr GeosShellMenuHitTest
   bcc +
   sta GeosMenuSelection
   jsr PreviewActivateApp
   rts
+
   jmp GeosMouseCloseOverlay
PreviewMouseDone:
   rts

PreviewActivateApp:
   lda GeosOverlayMode
   cmp #GeosOverlayMenu
   bne PreviewMouseDone
   lda GeosActiveMenu
   bne PreviewMouseDone
   lda GeosMenuSelection
   cmp #4
   bcc PreviewMouseDone
   jmp GeosShellMenuActivate

PreviewCopyApps:
   ldy #0
-  lda (PtrAddrLo),y
   sta (Ptr2AddrLo),y
   inc PtrAddrLo
   bne +
   inc PtrAddrHi
+  inc Ptr2AddrLo
   bne +
   inc Ptr2AddrHi
+  lda PtrAddrLo
   cmp #<PreviewAppsPayloadEnd
   bne -
   lda PtrAddrHi
   cmp #>PreviewAppsPayloadEnd
   bne -
   rts

; Snapshots after first home, second home, File menu, and final surface. These
; prove the observed mode at each completed redraw, not every intervening cycle.
; Each record is D011 (without raster high bit), D016, D018, CIA2 bank bits.
PreviewRecordMode:
   lda $d011
   and #$7f
   sta PreviewModes,x
   lda $d016
   sta PreviewModes+1,x
   lda $d018
   sta PreviewModes+2,x
   lda $dd00
   and #3
   sta PreviewModes+3,x
   rts
PreviewModes:
   !fill 16,0
PreviewReady:
   !byte 0
PreviewRuntimeEnd:
!if PreviewRuntimeEnd > $2000 {
   !error "Preview runtime overlaps the bitmap"
}

PreviewPayload:
   !binary "build/vice-preview/DesktopShellCode.bin"
PreviewPayloadEnd:
PreviewAppsPayload:
   !binary "build/vice-preview/GeosApps.bin"
PreviewAppsPayloadEnd:
PreviewDestinationEnd = MainCodeRAMStart + PreviewPayloadEnd - PreviewPayload
!if PreviewPayload > MainCodeRAMStart {
   !error "Preview runtime overlaps its copy destination"
}
!if PreviewDestinationEnd > $a000 {
   !error "Preview destination exceeds RAM below BASIC ROM"
}
