# TeensyROM Custom GUI firmware

[`TeensyROM+_0.8.0.4_CustomGUI_InputIEC_full.hex`](TeensyROM+_0.8.0.4_CustomGUI_InputIEC_full.hex)
is the current combined TeensyROM+ Fab0.4 experimental firmware. It includes
MinimalBoot, the MHS Power Engine services, and the matching C64 desktop.
Install the complete image: IEC launching requires a new firmware command,
so the desktop must not be paired with an older Teensy firmware.

- True 320x200 high-resolution bitmap desktop, with per-cell color pairs
- Pixel-drawn icons, two-line filenames, menus, clock, and SID play/pause
- Commodore 1351 mouse on port 1, joystick on port 2, and keyboard support
- Tighter icon hit targets, filtered mouse clicks, and Shift/cursor input fixes
- Drive 8/9 directories and PRG launching from opened D64/D71/D81 images
- Control Panel and the existing confirmed firmware-update path retained

Open an SD2IEC image, then its boot PRG. Standard $0801 BASIC/SYS boot programs
run automatically; other machine-code files load and require an explicit SYS.
Launching replaces the desktop. SD/USB image extraction is not drive mounting.
Copy, paste, delete, and other disk-write operations remain unavailable.

The build passed 89 focused automated tests, AGI firmware conformance, and
23 VICE disk-launch checks. These are not physical C64/SD2IEC acceptance:
the latest input changes and firmware still need real-hardware testing.
Keep official restore firmware available before flashing.

See [`CUSTOM-DESKTOP.md`](../docs/CUSTOM-DESKTOP.md) for behavior and limitations.
Only the latest combined image is retained; older checked-in versions remain
recoverable through Git history. Temporary local test/build output is not part
of the release; maintained regression tests remain in the source tree.

SHA-256 (also in [`SHA256SUMS.txt`](SHA256SUMS.txt)):

`b3e510b21f366fe1a922e4b4bceb39e3680bc92af0f5dfc3b81ab60ab3ad70af`
