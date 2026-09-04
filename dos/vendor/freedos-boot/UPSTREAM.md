# FreeDOS FAT16 boot sector

These standalone boot-loader sources come from the FreeDOS kernel repository:
https://github.com/FDOS/kernel/tree/d6791add2043c9d7b584d840a8ffaf8829fd2bdc/boot

`boot.asm` and `magic.mac` are unchanged upstream files. `COPYING` contains
their GNU GPL version 2 license; source comments permit version 2 or later.
They are assembled into the C: disk image, independently of the Teensy firmware.

Build with NASM 2.16.03 from the repository root:

```powershell
nasm -f bin -DISFAT16 -I dos/vendor/freedos-boot/ dos/vendor/freedos-boot/boot.asm -o dos/vendor/freedos-boot/boot16.bin
```

The 512-byte output has SHA-256:
`c78d072846e03ae940d9c4904c3805df577657f6ed9a286986a8278fe496f71d`.
The image builder checks that hash before filling the disk-specific BPB.
