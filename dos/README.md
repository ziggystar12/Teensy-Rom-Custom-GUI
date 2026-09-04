# DOSVM

DOSVM is part of TeensyROM alongside the GUI and MHS Power Engine. It runs
FreeDOS with CGA graphics, PC-speaker sound, keyboard input and writable SD
storage. DOSVM, Boulder and Might and Magic have been confirmed working on physical
hardware.
The current release uses firmware **V1.0.19** and its matching **R23** internal
cartridge revision. [Hardware notes](HARDWARE-TEST.md) record physical
baselines and the current regression checks.

## Install or upgrade

Use the single [DOSVM package](../DOSVM/README.md) at the repository root.

1. New users flash `DOSVM/firmware/MPE_Firmware-V1.0.19.hex`. If About already
   reports V1.0.19, keep that firmware installed.
2. For a first installation, copy `DOSVM/sd-card/` contents to the SD root.
   This installs `/DOSVM.CRT`, the fresh `/DOSVM/DOSVM.IMG` C: image, and
   `/DOSVM/D/` files.
3. For an upgrade, copy the new CRT but retain your C: image and D: files. If
   upgrading from a kit older than R20, also copy `DOSVM/D/DOSVMUPD/` and run
   `D:\DOSVMUPD\UPDDOS` once after booting; it replaces the former 40-column
   startup setting with the 80-column text setup. Follow
   [Upgrading DOSVM](STORAGE.md#upgrading-dosvm). Do not copy the fresh image
   over your working C: drive.
4. Launch `DOSVM.CRT` from the GUI. The startup page reads
   `Mean Hamster BIOS (C) 2026`, the 512K memory test and `Booting drive C:`
   with a short POST beep before FreeDOS starts.
5. At `C:\>`, try `DIR`, `DIR D:\`, `MEM`, `PCTONE`, then `BOULDER`.

The package contains firmware, CRT, a fresh disk template, SD-folder startup
files, instructions and checksums. Published cartridge/disk copies also live
under `dos/sd-card/`; [SHA256SUMS.txt](SHA256SUMS.txt) identifies those files.
Earlier release kits remain unchanged under `releases/`.

If an older GUI reports “Firmware selection changed” for the unchanged HEX,
press **V** and install it through the original text updater once. After
reboot, verify V1.0.19 in **TEENSY > About MPE Firmware**. The user confirmed
the automatic firmware-update flow worked with V1.0.15.

V1.0.19 uses the paired R23 CRT. Upgraders flash V1.0.19 and replace the CRT
without replacing either drive. R23 retains R22's physically confirmed
cold-start recovery and corrects the DOS backslash glyph in paths and the
`C:\>` prompt; physical confirmation of that glyph remains open.

## Drives and applications

C: is a writable **20 MiB FAT16 image**, with about 19 MiB available when fresh.
D: maps the ordinary SD folder `/DOSVM/D/`. Use DOS 8.3 names for games and
saves; for example, put `GAME.EXE` in `DOSVM/D/GAMES/`, then enter:

```dos
D:
CD \GAMES
GAME
```

`MEM`, `XCOPY`, `MORE` and `ATTRIB` are on PATH in `C:\FREEDOS\BIN`.
FreeCOM provides `COPY`, `DIR`, `TYPE`, `MD`, `RD`, `REN` and `DEL`.
[Storage instructions](STORAGE.md) cover saves, supported operations and limits.
Finish disk operations before resetting or removing power, and keep backups
of both the C: image and D: files.

## Controls, display and sound

In Boulder, press **Space to skip the intro, then Shift to start**.
Cursor keys move, Shift grabs and Space pauses. Port 2 joystick directions
act as cursor keys; fire acts as Shift. C64 Shift+cursor selects Up/Left.
Both Shift keys, Ctrl, Commodore/Alt and F1–F8 are mapped; F9 and higher are
not mapped. The joystick supplies keyboard state rather than a PC joystick.
See the [complete C64-to-PC keymap](KEYMAP.md), including punctuation and
unavailable PC keys.

DOS commands use a **black-and-white 80x25 console**. Two 4x8 glyphs share
each C64 hires cell, using the supplied 80columns font; the visible prompt has
a blinking underline cursor. This affects text mode only. CGA applications
continue to choose their own graphics mode and renderer.

By default, CGA modes 4/5 reduce 320x200 graphics to C64 160x200 logical
multicolour pixels. Fine lettering drawn as graphics can consequently look
double-width or lose gaps between strokes.

Press **Ctrl+Commodore+F7** to toggle the optional **sharp 320x200** renderer.
It preserves each of the 320 source pixels horizontally. The C64 hires display
allows only two colours in each 8x8 cell, so colourful areas may lose colour
detail. Toggle again to return to the default multicolour renderer. This is a
display choice for CGA applications generally; it does not change their video
mode or patch a particular game.

Mode 6 reduces 640x200 monochrome to 320x200 hires. Display start, blanking,
palette and intensity are reflected in the nearest C64 colours. Mode changes
replace the complete picture before displaying it. Ordinary CGA scrolling
repaints while the picture remains visible, fixing Boulder's black screen
during display-start changes.

`PCTONE` exercises PC-speaker pitch/gate output through SID voice 1. Rapid
changes can be coalesced at display-packet boundaries; this is not sampled
audio. The package uses NTSC SID pitch tuning; PAL pitch is slightly lower.
[Tandy graphics](TANDY-VIDEO-PLAN.md), EGA and VGA remain planned work.

## Memory and execution

The guest owns all **512 KiB of RAM2** directly. There is no page cache or
`DOSVM.SWP`, and neither drive is loaded into guest RAM. DOS and its resident
folder driver consume part of conventional memory; use `MEM` for the current
free memory and largest-program figures. Optional PSRAM is not required.

Firmware code remains in flash while live DOS, SD and MPE state uses RAM1.
DOS is a reset-only session: leaving it resets TeensyROM to the GUI before
another application can reuse its memory. Quiet packet recovery introduced
in R17 is retained; repeated unrecoverable errors produce a diagnostic.

## Build and automated checks

From the repository root:

```powershell
.\dos\tools\build_dosvm.ps1
```

The builder reuses `build/dos-work/`, reads `firmware-version.json`, and
publishes `DOSVM/` after its checks pass. Source inputs default to the FreeDOS
archive in `E:\MHS-Repository\HamsterOS\build\freedos\FDT2607-FloppyEdition.zip`
and `E:\MHS-Repository\HamsterOS\dos\Boulder.exe`. `-FreeDosZip`, `-Boulder`,
`-Compiler` and `-ToolchainRoot` override these locations. Python, Node.js, a
Windows C++ compiler, the firmware toolchain and sibling AGI-64 sources are
required.

To repeat the real FreeDOS drive checks:

```powershell
.\dos\tools\test_mpe5_core_services.ps1 -Image .\DOSVM\sd-card\DOSVM\DOSVM.IMG
```

Automated checks cover boot, commands, C/D file operations, executable loading,
saves and restart persistence, seek/truncate/commit, input, graphics, speaker,
packet recovery and Sierra regression. [HARDWARE-TEST.md](HARDWARE-TEST.md)
records physical results, historical failures and repeatable regression checks.
