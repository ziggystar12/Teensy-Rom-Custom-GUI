; Standalone BASIC-loadable wrapper for the enhanced desktop shell.
; The embedded payload is assembled separately at MainCodeRAMStart.

!convtab pet
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
   lda #<DesktopShellPayload
   sta PtrAddrLo
   lda #>DesktopShellPayload
   sta PtrAddrHi

   lda #<MainCodeRAMStart
   sta Ptr2AddrLo
   lda #>MainCodeRAMStart
   sta Ptr2AddrHi

   ldy #0
CopyDesktopShellByte:
   lda (PtrAddrLo),y
   sta (Ptr2AddrLo),y

   inc PtrAddrLo
   bne +
   inc PtrAddrHi
+
   inc Ptr2AddrLo
   bne +
   inc Ptr2AddrHi
+
   lda PtrAddrLo
   cmp #<DesktopShellPayloadEnd
   bne CopyDesktopShellByte
   lda PtrAddrHi
   cmp #>DesktopShellPayloadEnd
   bne CopyDesktopShellByte

   jmp MainCodeRAMStart

DesktopShellPayload:
   !binary "build/DesktopShellCode.bin"
DesktopShellPayloadEnd:

!if DesktopShellPayloadEnd > MainCodeRAMStart {
   !error "Desktop shell payload overlaps its destination"
}
