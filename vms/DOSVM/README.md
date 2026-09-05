# DOSVM — original Tandy default + optional F5 (V1.1.7 firmware)

This update keeps the original Tandy conversion and changed-cell transfers
by default, and restores **F5 as an explicit opt-in** to shared Enhanced-25.
It supersedes the temporary byte-identical rollback from `ef9cc9114fad`.
The V1.1.7 firmware remains unchanged; **no reflash is required**.
NESVM/GBVM shared video modes are unaffected.

Install [MPE Firmware V1.1.7](../../firmware/MPE_Firmware-V1.1.7.hex), then extract
[DOSVM.zip](../DOSVM.zip) to the SD root for a **fresh installation**. Launch
`DOSVM.crt` in the GUI. The package's `client.crt` also works as a launcher.
DOS should show its POST page with 512K, then boot FreeDOS to `C:\>`.

## Preserve existing games and saves

The ZIP contains a fresh 20 MiB C: disk template. **Never overwrite your working
image or replace your D: folder with a fresh template.** Back up both first.
For an existing ABI 2 installation, keep V1.1.7 and extract
[DOSVM-update.zip](../DOSVM-update.zip) to the SD root. It updates the launcher,
client and engine together and contains **no C: image or D: files**. The earlier
GRAPHSET save fix is retained. Replace both the launcher/client and engine.

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

## Graphics controls

Boot, the DOS prompt, and default graphics keep the pre-port renderer and
CELL/SID transfers. Shared conversion and DMA begin only after you select F5
in graphics (or select it at the prompt for the next graphics screen).

| Guest graphics | Default C64 output |
| --- | --- |
| CGA 320-wide | Original multicolor reduction |
| CGA 640-wide monochrome | 320-wide hires, retaining thin strokes |
| Tandy 08h, 160-wide | Original double-width multicolor |
| Tandy 09h, 320-wide | Original 320-wide hires, automatic |

Hold **Commodore + Control**, then press:

- **F1:** original/default DOS renderer, including automatic hires Tandy 09h.
- **F5:** shared Enhanced-25, 320-wide output with additional raster color splits.
- **F7:** original Sharp toggle for CGA 4/5; from F5, returns to original Sharp.

Tandy 09h stays hires in the original renderer regardless of the Sharp toggle.
F3 is not enabled in this focused update. Unmodified F1/F5/F7 still reach DOS.
Returning to text uses the original text renderer; F5 remains selected for the
next graphics screen until F1/F7 or a guest reboot. Switching out of F5 waits
for the current shared frame's acknowledgment before rebuilding the original
display. A physically stalled cartridge link is not a guaranteed hotkey recovery.
Do not use this DOS-specific update to replace the NES or GB packages.

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
The opt-in verifier checks the unchanged original converter, replays real
default module packets through the 6510 receiver, and tests F5/F1 with the
actual firmware service. PAL/NTSC VICE tests exercise the enhanced receiver.
ARM code is 88,984 bytes; static RAM1 is 9,088 bytes, leaving 187,520 bytes
of module workspace (175,464 used). The two renderers share one video arena.
Physical startup, speed, sound and sustained game compatibility remain open.
See [F5 opt-in notes](../../docs/Architecture/DOS-F5-OPT-IN.md).

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
