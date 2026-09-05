# DOSVM — shared-video V1.1.7 test

Install [MPE Firmware V1.1.7](../../firmware/MPE_Firmware-V1.1.7.hex), then extract
[DOSVM.zip](../DOSVM.zip) to the SD root for a **fresh installation**. Launch
`DOSVM.crt` in the GUI. The package's `client.crt` also works as a launcher.
DOS should show its POST page with 512K, then boot FreeDOS to `C:\>`.

## Preserve existing games and saves

The ZIP contains a fresh 20 MiB C: disk template. **Never overwrite your working
image or replace your D: folder with a fresh template.** Back up both first.
For an existing ABI 2 installation, install V1.1.7 and extract
[DOSVM-update.zip](../DOSVM-update.zip) to the SD root. It updates the launcher,
client and engine together and contains **no C: image or D: files**. The earlier
GRAPHSET save fix is retained. This is no longer an engine-only update.

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
writable C: and D:. Ctrl+Commodore+Del
restarts the guest. Hardware reset returns to the GUI. Finish writes before
resetting: reset-only operation does not make interrupted filesystem writes safe.

## Shared graphics controls

Hold **Commodore + Control**, then press an unshifted function key:

| Key | Output |
| --- | --- |
| F1 | Default: wide multicolor pixels (startup default) |
| F3 | Auto-8: automatic enhanced color selection |
| F5 | Enhanced-25: hires with raster color changes |
| F7 | Sharp: hires, two colors per 8x8 cell |

These select a mode directly; F7 is no longer a toggle. CGA and Tandy use the
same firmware service as NESVM. The 80-column DOS text renderer is unchanged;
AGI keeps its separate video solution. Tandy 160-wide input retains double-width
pixels. 320-wide input fills the screen; 640-wide monochrome is reduced to 320.

The shared converter retains DOS's foreground-preserving CGA reduction,
nonblack CGA backgrounds, RGBI color mapping, and thin-line-preserving monochrome
reduction. DOS still owns video registers, VRAM banking and palette changes.
The firmware owns C64 conversion, raster effects, buffering and mode hotkeys.
F5 uses the optional 36 KiB changed-area cache; dense animation/scrolling can
still lower picture cadence. Physical flashing/cadence/input checks are pending.
F3/F5 also retain the raster trick's left-edge artifact on full-width DOS
images; F7 avoids that tradeoff.

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
All four shared modes pass the module test. Shared F1 CGA and Sharp monochrome
output match the old DOS renderer pixel-for-pixel across 192 palette cases.
ARM code is 85,200 bytes; static RAM1 is 9,056 bytes. Measured workspace use is
169,416 of 187,552 bytes (18,136 spare), including the 36 KiB video cache.
Physical startup, speed, sound and sustained game compatibility remain open.
See [test report](../../docs/Architecture/DOS-MODULAR-TEST-STATUS.md).

## GRAPHSET save fix

The D: redirector now permits a DOS process to create a file and reopen it in
compatibility mode while retaining the first handle. Explicit deny modes and
other-process sharing checks remain enforced. Create/truncate metadata is
flushed before another handle opens the file.

Local tests passed with the supplied GRAPHSET: Tandy on C:, then Tandy/CGA/Tandy
on D:, verifying the saved byte through guest readback. A separate original
test repeats the create/reopen/write/exit sequence 20 times without leaking
handles. Please retest GRAPHSET on hardware; local execution is not SD-card
or physical gameplay acceptance.
