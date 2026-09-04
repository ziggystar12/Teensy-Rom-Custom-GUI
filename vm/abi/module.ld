MEMORY {
  CODE (rx) : ORIGIN = 0x00020000, LENGTH = 128K
  DATA (rw) : ORIGIN = 0x20200000, LENGTH = 512K
}
ENTRY(vm_entry)
SECTIONS {
  .text : { KEEP(*(.entry)) *(.text*) *(.rodata*) . = ALIGN(4); } > CODE
  .data : { *(.data*) . = ALIGN(32); } > DATA
  .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(32); } > DATA
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.eh_frame*) }
  ASSERT(SIZEOF(.text) <= 128K, "VM code does not fit RAM1 module window")
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) < 512K, "VM has no remaining system RAM")
}
