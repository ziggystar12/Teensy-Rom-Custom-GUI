# NESVM — modular ABI 2

Download [NESVM.zip](../NESVM.zip) and extract it to the SD root. Install
[MPE Firmware V1.1.4](../../firmware/) or a newer compatible generic host.
Launch `NESVM.crt`, or select a `.nes` file in the GUI. If copying this folder
manually to `/VMS/NESVM`, its `client.crt` is also a launchable cartridge.

V1.1.4 corrects emulation starvation and gives game/music time priority over
expensive display transfers. It includes the picker fix and centered F7.
Update BOTH firmware and this package, including from V1.1.3. The client is
unchanged, but the NES engine has changed. [Timing regression](../../docs/Architecture/NES-TIMING-V1.1.4.md).

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
- **F5:** Enhanced-25 (enhance every useful band automatically).
- **F7:** Sharp (320x200 hires canvas, centered 256-wide NES image with 32 black
  columns on each side; two colors per 8x8 cell). Vertical fit remains 240-to-200.

The picker keeps sharp text regardless of gameplay selection. F3/F5 use two
color pairs split vertically in selected bands, not arbitrary four-color
placement. They can be slower/flicker and show a leftmost 24-pixel FLI artifact.
Use F1 or F7 to compare. No game-by-game setup is needed; MPE firmware converts
all native NES frames and decides where enhancement helps.

This main-branch package includes the fast DMA path, optimized NES core,
idle-picker input fix and V1.1.4 emulation-first timing without a duplicate
SID heartbeat flood. Code, support state, renderer and menus use RAM1;
NES CPU/PPU memory and loaded cartridge data use the full 512 KiB RAM2 arena.
Only one VM is loaded at a time. Hardware speed and picture quality need a
retest; successful host/emulator checks do not establish physical performance.

Update firmware AND this package together, including the root NESVM.crt.
V1.1.1 (including the fast-test image) lacks the indexed service required here.
Only the authorized Crossbow demo is packaged; preserve your private ROMs.

See the [input regression report](../../docs/Architecture/NES-PICKER-INPUT-FIX.md)
for keyboard/joystick coverage and the remaining hardware acceptance check.
