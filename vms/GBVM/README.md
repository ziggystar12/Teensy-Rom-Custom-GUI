# GBVM first hardware candidate

Requires MPE firmware **V1.1.6**. Extract the download to the SD root. Put your
own `.gb` and `.gbc` ROMs in `/VMS/GBVM/ROMS`, then launch `GBVM.crt`.
Both extensions can also launch directly through the firmware file browser.
No commercial ROMs are supplied or copied from the local test folder.

Default F1 preserves all 160 x 144 Game Boy pixels: each is two C64 pixels
wide, with 28 black rows above and below. Original GB retains four shades;
GBC colors are reduced by the shared firmware renderer. Commodore + Control
+ unshifted F1/F3/F5/F7 selects Default / Auto-8 / Enhanced-25 / Sharp.
The sharper modes can sacrifice colors; F1 is recommended for original GB.
These controls do not change AGI's existing presentation.

Joystick port 2/cursors select and move; Fire = A, Space = B, Return = Start,
Shift = Select. Start + Select returns to the picker. Shifted cursors are
Up/Left, not simultaneous Select. The picker supports up to 128 ROMs.

Initial supported cartridges: type 00, type 01 (MBC1), type 19 (MBC5),
up to 512 KiB, without cartridge RAM. **Battery saves, RTC and other mapper
types are not ready and are rejected.** This is not all-ROM compatibility.
Sound is a SID approximation, not exact Game Boy PCM/stereo.

GNU GPL version 2 or later; no warranty. LICENSE-gnuboy.txt and SOURCE contain
the license and complete buildable corresponding source. Physical speed,
audio quality and controller acceptance still require testing on the C64.
