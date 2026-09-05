# NESVM — modular ABI 2

Download [NESVM.zip](../NESVM.zip) and extract it to the SD root. Install
[MPE Firmware V1.1.1](../../firmware/) or a newer compatible generic host.
Launch `NESVM.crt`, or select a `.nes` file in the GUI. If copying this folder
manually to `/VMS/NESVM`, its `client.crt` is also a launchable cartridge.

Put your ROMs in `/VMS/NESVM/ROMS/`. Crossbow is the supplied authorized demo;
SMB and other private games are not bundled. Current support: NTSC mapper 0/11.

Port-2 joystick Up/Down changes rows; Left/Right changes pages. Keyboard cursor
Down/Right does the same; either Shift reverses it to Up/Left. Shift used with
a cursor does not also trigger Select. Port-2 Fire is A, Space is B, Return is
Start, and standalone Shift is Select. Start+Select returns to the NES picker.
Ctrl+Commodore+F7 toggles sharp rendering. Reset returns to the GUI.

This main-branch package includes the fast DMA path, optimized NES core and
idle-picker input fix. Code, support state, renderer and menus use RAM1;
NES CPU/PPU memory and loaded cartridge data use the full 512 KiB RAM2 arena.
Only one VM is loaded at a time. Hardware speed and picture quality need a
retest; successful host/emulator checks do not establish physical performance.

Use the current firmware download, SHA-256
`90dbbce97b5e40b4e77c37902e2407711ef6f36c1421c15c7aaac48d28991a8b`.
It is the same image as the September 4 fast-test kit, so existing fast-test
users only need this NES package. The older pre-DMA V1.1.1 firmware is not
sufficient. New indexed-video modes are a separate follow-on release.

See the [input regression report](../../docs/Architecture/NES-PICKER-INPUT-FIX.md)
for keyboard/joystick coverage and the remaining hardware acceptance check.
