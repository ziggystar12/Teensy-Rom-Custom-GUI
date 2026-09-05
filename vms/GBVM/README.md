# GBVM — Mario 2 / Kirby / Zelda cartridge update

Requires MPE firmware **V1.1.6 or newer**; this update targets existing V1.1.7,
with **no firmware reflash**. Extract the download to the SD root. Put your
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

The limit remains **512 KiB (524,288 bytes) per ROM**, for both GB and GBC.
Supported cartridges: type 00; MBC1 types 01/02/03; MBC5 types 19/1A/1B.
RAM-bearing cartridges currently require **8 KiB SRAM**. MBC1/5 battery
variants now save that SRAM. Other RAM sizes, RTC and other mapper types
remain unsupported and are rejected. This is not all-ROM compatibility.
Sound is a SID approximation, not exact Game Boy PCM/stereo.

Mario 2 and original Zelda/Link's Awakening (512 KiB, MBC1 + 8 KiB battery RAM)
passed local core/module tests, as did Kirby's Dream Land (256 KiB, MBC1).
Mario 1 and the supplied 256 KiB Pac-Man GBC remain regression-tested.
Zelda DX (1 MiB, 32 KiB RAM) does not fit this release. File extension alone
does not establish cartridge compatibility. Physical playtesting remains open.

## Battery saves

Changed battery RAM is checkpointed approximately every five seconds at a
safe module boundary, and when Start + Select returns to the picker. Return
to the picker before resetting/powering off. A reset can lose recent changes.
Save failures return to the picker and block loading another ROM until Fire
successfully retries; keep power on if the picker reports a save failure.

`/VMS/GBVM/SAVES/<ROM-CRC32>.s0` and `.s1` are alternating, length/CRC-checked
slots. The last verified slot is retained during replacement. Back up the
whole SAVES directory. Damaged slots fall back to a valid partner; two damaged
slots stop loading rather than silently starting over. This is not a guarantee
against filesystem/media failure. Existing raw `.sav` files from other emulators
are **not imported or overwritten** by this update. ROM files are read-only.

The ZIP includes neither games nor saves and does not clear your ROMS/SAVES
folders. Replace the module and launcher/client together; leave other VM
packages alone. See [verification notes](../../docs/Architecture/GBVM-BATTERY-CARTS.md).

GNU GPL version 2 or later; no warranty. LICENSE-gnuboy.txt and SOURCE contain
the license and complete buildable corresponding source. Physical speed,
audio quality and controller acceptance still require testing on the C64.
