# DOSVM — modular ABI 2 hardware-speed test

Install [MPE Firmware V1.1.1](../../firmware/), then extract
[DOSVM.zip](../DOSVM.zip) to the SD root for a **fresh installation**. Launch
`DOSVM.crt` in the GUI. The package's `client.crt` also works as a launcher.
DOS should show its POST page with 512K, then boot FreeDOS to `C:\>`.

## Preserve existing games and saves

The ZIP contains a fresh 20 MiB C: disk template. **Never overwrite your working
image or replace your D: folder with a fresh template.** Back up both first.
For an existing modular installation use [DOSVM-update.zip](../DOSVM-update.zip),
which contains only launcher, engine, manifest and BIOS; it contains no disk.

When moving from the old built-in DOSVM, copy (do not delete/move) your working
`/DOSVM/DOSVM.IMG` to `/VMS/DOSVM/DOSVM.IMG`, and copy your `/DOSVM/D/` files to
`/VMS/DOSVM/D/`. Keep the originals as a backup. Do not extract the fresh image
over that copied working image. No legacy firmware/client is required.

| Drive | SD location |
| --- | --- |
| C: | `/VMS/DOSVM/DOSVM.IMG` — writable FAT16 image |
| D: | `/VMS/DOSVM/D/` — ordinary files, DOS 8.3 names |

## Compare speed

At the prompt try `DIR`, `DIR D:\`, `MEM`, `PCTONE` and `BOULDER`. In Boulder,
Space skips the intro and Shift starts. Port-2 directions act as cursor keys;
Fire acts as Shift. Compare prompt/scrolling, screen-update speed and gameplay
with the earlier DOS build. Please note PAL/NTSC and the exact game tested.

Included: existing 8086/PC-XT CPU, 80-column text, CGA and Tandy modes 08/09,
PC speaker, Tandy three-voice SID translation, held keyboard/joystick input,
writable C: and D:. Ctrl+Commodore+F7 toggles sharp graphics; Ctrl+Commodore+Del
restarts the guest. Hardware reset returns to the GUI. Finish writes before
resetting: reset-only operation does not make interrupted filesystem writes safe.

## Memory and evidence

All executable code, emulator state, BIOS backing, video buffers and folder
support are in RAM1. The full 524,288-byte RAM2 is conventional guest RAM.
Only one VM loads at a time; NES and DOS do not share RAM while running.

The compact RAM1 PC device map backs the 32 KiB CGA/Tandy aperture, two 4 KiB
BIOS text shadows and low 4 KiB I/O ports. Unimplemented expansion memory is
absent; high I/O ports return FF, not aliases of low devices. There is no EMS,
XMS, Hercules or arbitrary expansion-hardware claim.

Host tests boot the actual module, write/re-read C:/D:, execute a DOS COM
program in both Tandy modes, return to text and verify memory/packet guards.
Physical startup, speed, sound and sustained game compatibility remain open.
See [test report](../../docs/Architecture/DOS-MODULAR-TEST-STATUS.md).
