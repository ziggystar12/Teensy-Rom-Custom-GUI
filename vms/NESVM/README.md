# NESVM — enhanced-video cadence candidate (modular ABI 2)

Download [NESVM.zip](../NESVM.zip) and extract it to the SD root. Install
[MPE Firmware V1.1.7](../../firmware/) for the combined shared-video release.
Launch `NESVM.crt`, or select a `.nes` file in the GUI. If copying this folder
manually to `/VMS/NESVM`, its `client.crt` is also a launchable cartridge.

This package replaces the cycle/dot-stepped core with Nofrendo's
instruction/scanline CPU and PPU. The picker now says **NESVM NOFRENDO**.
Install both V1.1.7 firmware and this NESVM package; retain your ROMs.
F3/F5 now retain exact copies of both display banks and upload changed bytes
only. Unchanged raster plans are not resent. Dense scrolling still costs more
than a static background; this is not a promise of 60 displayed frames/sec.
The full-speed Nofrendo core and F1/F7 transfer paths are unchanged.
F5 and F7 retain their centered image and inactive-bank updates remain atomic.

Put your ROMs in `/VMS/NESVM/ROMS/`. Crossbow is the supplied authorized demo;
SMB and other private games are not bundled. Current support: NTSC mapper 0/11.

Port-2 joystick Up/Down changes rows; Left/Right changes pages. Keyboard cursor
Down/Right does the same; either Shift reverses it to Up/Left. Shift used with
a cursor does not also trigger Select. Port-2 Fire is A, Space is B, Return is
Start, and standalone Shift is Select. Start+Select returns to the NES picker.
Reset returns to the GUI. Hold **Commodore + Control** and press an unshifted
function key for a direct gameplay video selection:

- **F1:** Default (ordinary wide-pixel multicolor).
- **F3:** Auto-8 (enhance up to eight useful bands automatically).
- **F5:** Enhanced-25 (enhance every useful band; centered 256-wide NES image).
- **F7:** Sharp (320x200 hires canvas, centered 256-wide NES image with 32 black
  columns on each side; two colors per 8x8 cell). Vertical fit remains 240-to-200.

The picker keeps sharp text regardless of gameplay selection. F3/F5 use two
color pairs split vertically in selected bands, not arbitrary four-color
placement. Their picture cadence is lower and the leftmost 24-pixel FLI artifact
remains; F5's black side margin keeps NES content outside that artifact area.
The first image/mode transitions can briefly blank; steady F3/F5 updates do not.
Use F1 or F7 to compare. No game-by-game setup is needed; MPE firmware converts
all native NES frames and decides where enhancement helps.

This main-branch package retains fast DMA, the idle-picker fix and emulation-first
timing. Code, hot CPU/PPU state, renderer and menus use RAM1; loaded ROM data
uses RAM2. Only one VM is loaded at a time. The Nofrendo host comparison ran
about 4.6x faster for Crossbow and 6.0x for Popeye at equal emulated-cycle counts.
These are desktop core measurements, **not measured Teensy speedups**. The user
subsequently reported full speed in F1/F7, with stable but uneven F5 cadence.
The changed-area F5 optimization still needs physical retesting. Approximate SID sound and explicit errors
for unsupported DMC/ROM profiles remain; this is not an all-mapper upgrade.

The previous V1.1.5 hardware result was SPEED 35%, RUN 91%, HOST 9%.
F5 was physically confirmed working and looking good before this core swap.
Play for at least 10 seconds, then Return+Shift (Start+Select) returns to the
picker. Its bottom status line reports `SPEED ...% RUN ...% HOST ...%` from the
last two-second window. SPEED measures emulated time; RUN includes core work
and interrupts during it; HOST includes video, transport and other time.
Send that line and the selected F-key mode with your result.

The ZIP includes Nofrendo's GNU Library GPL v2 license and complete relinkable
module sources in `SOURCE/`. See `SOURCE/README.md` to rebuild a modified core.
GB/GBC use the separate GBVM candidate and the same firmware services, not
this NES core. Only the authorized Crossbow demo is packaged here.

[Candidate measurements and acceptance checklist](../../docs/Architecture/NES-NOFRENDO-CANDIDATE1.md).

See the [input regression report](../../docs/Architecture/NES-PICKER-INPUT-FIX.md)
for keyboard/joystick coverage and the remaining hardware acceptance check.
