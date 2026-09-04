# NESVM — modular baseline 1

Download [NESVM.zip](../NESVM.zip) and extract it to the SD root. Install
[MPE Firmware V1.1.0](../../firmware/) or a newer compatible generic host.
Launch `NESVM.crt`, or select a `.nes` file in the GUI. If copying this folder
manually to `/VMS/NESVM`, its `client.crt` is also a launchable cartridge.

Put your ROMs in `/VMS/NESVM/ROMS/`. Crossbow is the supplied authorized demo;
SMB and other private games are not bundled. Current support: NTSC mapper 0/11.

Up/Down changes rows; Left/Right changes pages. Port-2 Fire is A, Space is B,
Return is Start, Shift is Select. Start+Select returns to the NES picker.
Ctrl+Commodore+F7 toggles sharp rendering. Reset returns to the GUI.

Hardware status: user-confirmed SMB launch, but severe slowdown. This package
preserves that working baseline for later performance work; it is not optimized.
