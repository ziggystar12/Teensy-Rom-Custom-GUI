# Historical Desktop Apps firmware notes

These notes describe the earlier Desktop Apps build. For the current combined
GUI and native MHS Power Engine, use the [current firmware](../firmware/README.md) and
[File Operations guide](FILE-OPERATIONS.md). Native08 includes Copy, Paste,
and permanent Delete as well as the apps described below.

`TeensyROM+_0.8.0.4_CustomGUI_DesktopApps_full.hex`, preserved in
[Git history](https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/blob/c2025f1/firmware/TeensyROM+_0.8.0.4_CustomGUI_DesktopApps_full.hex),
was the Desktop Apps TeensyROM+ Fab0.4 experimental firmware. It included
MinimalBoot, the MHS Power Engine services, and the matching C64 desktop.
Install the complete image: IEC launching requires a new firmware command,
so the desktop must not be paired with an older Teensy firmware.

- True 320x200 high-resolution bitmap desktop, with per-cell color pairs
- Pixel-drawn icons, two-line filenames, menus, clock, and SID play/pause
- Commodore 1351 mouse on port 1, joystick on port 2, and keyboard support
- Tighter icon hit targets, filtered mouse clicks, and Shift/cursor input fixes
- Drive 8/9 directories and PRG launching from opened D64/D71/D81 images
- Control Panel and the existing confirmed firmware-update path retained
- Native B&W Snake, integer Calculator, and Text Viewer in the Teensy menu
- Drawn window/close/up/page controls; colors applied after the new icons

The three demo apps close back into the UI without a cartridge reset. Snake
accepts keyboard, joystick, and mouse controls. Calculator uses signed 16-bit
integers, with overflow/division-by-zero errors. Text Viewer opens TXT/NFO/MD/
SEQ files from Teensy memory, SD, or USB, with previous/next paging; raw IEC
SEQ viewing is not connected yet. The classic list retains its legacy viewer.

Open an SD2IEC image, then its boot PRG. Standard $0801 BASIC/SYS boot programs
run automatically; other machine-code files load and require an explicit SYS.
Launching replaces the desktop. SD/USB image extraction is not drive mounting.
Copy, paste, delete, and other disk-write operations were unavailable in that build.

The build passed all 142 focused tests and AGI firmware conformance. Actual
VICE checks cover app rendering/input, calculator results, Snake movement,
close-to-desktop, and the new file-window controls. Ordinary Snake movement
redraws in about 0.16 seconds on emulated PAL hardware. These checks are not
physical C64/SD2IEC acceptance; the firmware still needs real-hardware testing.
Keep official restore firmware available before flashing.

See [`CUSTOM-DESKTOP.md`](CUSTOM-DESKTOP.md) for behavior and limitations.
The current combined image is linked from the [firmware index](../firmware/README.md).
Earlier checked-in versions remain recoverable through Git history. Temporary
local test/build output is not part of the release; maintained regression tests
remain in the source tree.

Historical Desktop Apps firmware SHA-256:

`222e9626cc8bbc543f4248d27f870827c38877bbe7947c241f68474080e6934e`
