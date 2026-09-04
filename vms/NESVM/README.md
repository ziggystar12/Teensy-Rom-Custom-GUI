# NESVM — modular ABI 2

Download [NESVM.zip](../NESVM.zip) and extract it to the SD root. Install
[MPE Firmware V1.1.1](../../firmware/) or a newer compatible generic host.
Launch `NESVM.crt`, or select a `.nes` file in the GUI. If copying this folder
manually to `/VMS/NESVM`, its `client.crt` is also a launchable cartridge.

Put your ROMs in `/VMS/NESVM/ROMS/`. Crossbow is the supplied authorized demo;
SMB and other private games are not bundled. Current support: NTSC mapper 0/11.

Up/Down changes rows; Left/Right changes pages. Port-2 Fire is A, Space is B,
Return is Start, Shift is Select. Start+Select returns to the NES picker.
Ctrl+Commodore+F7 toggles sharp rendering. Reset returns to the GUI.

The V1.1.0/ABI 1 baseline launched SMB on user hardware, but showed severe
slowdown and visibly drawing blocks of lines. This ABI 2 package moves the
emulator's support state, renderer and menus to RAM1. NES CPU/PPU memory and
loaded cartridge data are in RAM2. No speed or visual-quality fix is claimed.
The new memory layout still needs a hardware retest.
