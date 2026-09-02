; Standalone BASIC-loadable wrapper for the enhanced desktop shell.
; The embedded payload is assembled separately at MainCodeRAMStart.

!convtab pet
!set DesktopShell=1
!src "source/CommonDefs.i"

* = $0801

; 10 SYS2061 ($080d)
!word BasicProgramEnd
!word 10
!byte $9e
!text "2061"
!byte 0
BasicProgramEnd:
!word 0

DesktopShellLoader:
   sei
   cld
   ;The larger payload overlaps its destination. Copy end-to-start so each
   ;source byte is consumed before a higher destination byte replaces it.
   lda #<DesktopShellPayloadEnd
   sta PtrAddrLo
   lda #>DesktopShellPayloadEnd
   sta PtrAddrHi

   lda #<DesktopShellDestinationEnd
   sta Ptr2AddrLo
   lda #>DesktopShellDestinationEnd
   sta Ptr2AddrHi

   ldy #0
CopyDesktopShellByte:
   lda PtrAddrLo
   bne +
   dec PtrAddrHi
+
   dec PtrAddrLo
   lda Ptr2AddrLo
   bne +
   dec Ptr2AddrHi
+
   dec Ptr2AddrLo
   lda (PtrAddrLo),y
   sta (Ptr2AddrLo),y

   lda PtrAddrLo
   cmp #<DesktopShellPayload
   bne CopyDesktopShellByte
   lda PtrAddrHi
   cmp #>DesktopShellPayload
   bne CopyDesktopShellByte

   jmp MainCodeRAMStart

DesktopShellPayload:
   !binary "build/DesktopShellCode.bin"
DesktopShellPayloadEnd:
DesktopShellDestinationEnd = MainCodeRAMStart + DesktopShellPayloadEnd - DesktopShellPayload

!if DesktopShellPayload > MainCodeRAMStart {
   !error "Desktop shell loader overlaps its copy destination"
}
!if DesktopShellDestinationEnd > $a000 {
   !error "Desktop shell destination exceeds RAM below BASIC ROM"
}
