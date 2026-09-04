# The Black Cauldron: MPE demo

[Download The-Black-Cauldron-MPE.crt](The-Black-Cauldron-MPE.crt?raw=true)
and play on a C64/128 with TeensyROM+ Fab0.4 and Teensy 4.1. This current
M4G2 build includes native VIEW sidecars, restored main-character sprites,
mouse support, and the regular C64 menu controls. No game compilation is
needed.

## Start playing

1. Install the [current MPE firmware](../firmware/README.md), version V1.0.21
   or newer. Follow the
   [firmware installation guide](../docs/FIRMWARE-GUIDE.md#install-the-custom-firmware).
2. Copy `The-Black-Cauldron-MPE.crt` to the TeensyROM+ **SD card**. A folder
   named `Demo` on the card is fine.
3. Launch the CRT from the TeensyROM menu. Release the launch key, then press
   **RETURN** when the game's title asks for a key.

The cartridge requires an SD-card launch and MPE firmware with M4G2 support.
Stock TeensyROM firmware and VICE cannot run its native gameplay.

## Controls

Use the cursor keys or a joystick in **port 2** to move. An optional 1351 mouse
goes in **port 1**. **RUN/STOP** opens the game's menus; use cursor keys and
**RETURN** to select an item. The Speed menu offers Normal, Slow, Fast, and
Fastest. The original Help screen may still describe PC keys.

| Action | C64 shortcut |
| --- | --- |
| Help | F1 |
| Sound on/off | F2 |
| See object | F4 |
| Save | F5 |
| Restore | F6 |
| Restart | F7 |
| Change speed | F8 |
| New object | Commodore + F3 |
| Use object | Commodore + F4 |
| Do | Commodore + F6 |
| Look | Commodore + F8 |

F2/F4/F6/F8 mean **Shift + F1/F3/F5/F7** respectively. Keep Shift held for
those even-numbered keys when also holding the Commodore key.

This edition uses twelve save slots in `SAVES/BC1A6401.SAV` through
`SAVES/BC1A6412.SAV`. Earlier M4G1 saves are intentionally kept separate and
are not migrated to the M4G2 format.

## Source and credits

MPE/C64 adaptation, firmware, and GUI: **John Swiderski - Mean Hamster
Software**. Learn about the compiler at
[AGI-64](https://meanhamster.com/games/agi-64).

The Black Cauldron is Al Lowe's Sierra game based on Disney's film. The
original game content retains its original ownership; this conversion does
not change those rights. Exact source fingerprints and cartridge layout are
recorded in [manifest.json](manifest.json).

## Build and verification

The cartridge was built from AGI-64 revision
`6d050b2b0a9e0c8f34d4bc424f52622c87dcb5e0` with sprites, mouse input, and
C64 menus enabled. It contains 388 resources, including 25 native VIEW
sidecars. Build checks validate every resource and package CRC and reconstruct
every CRT CHIP for an exact comparison with the physical image. The DOSVM
firmware build also launches this exact M4G2 cartridge through the integrated
firmware module as a cold-start regression.

These are deterministic host and packaging checks. A complete playthrough and
physical C64/TeensyROM+ acceptance remain separate.

[SHA256SUMS.txt](SHA256SUMS.txt) identifies the exact cartridge bytes.

To rebuild with an AGI-64 checkout, use `config/bc-64.json` and run its normal
MPE cartridge build. Distribute the resulting `.crt`; raw images, resource
packages, and build intermediates are not needed on the SD card.
