# The Black Cauldron: MPE demo

[Download The-Black-Cauldron-MPE.crt](The-Black-Cauldron-MPE.crt?raw=true)
and play on a C64/128 with TeensyROM+ Fab0.4 and Teensy 4.1.
This build uses the restored main-character sprites and the regular C64
menu controls. It includes the packet read retry added for V1.0.2.
No game compilation is needed.

## Start playing

1. Install [MPE Firmware V1.0.4](../firmware/MPE_Firmware-V1.0.4.hex?raw=true)
   or a compatible later version. Follow the
   [firmware installation guide](../docs/FIRMWARE-GUIDE.md#install-the-custom-firmware).
2. Copy `The-Black-Cauldron-MPE.crt` to the TeensyROM+ **SD card**. A folder
   named `Demo` on the card is fine.
3. Launch the CRT from the TeensyROM menu. Release the launch key, then press
   **RETURN** when the game's title asks for a key.

This cartridge requires **MPE Firmware V1.0.2 or later** and an SD-card launch.
V1.0.2 includes the native input timing fix required for this release. Stock
TeensyROM firmware and VICE cannot run this cartridge's gameplay.

## Controls

Use the cursor keys or a joystick in **port 2** to move. An optional 1351 mouse
goes in **port 1**. **RUN/STOP** opens the game's menus; use cursor keys and
**RETURN** to select an item. The menu shortcuts match the C64 keyboard, and
the Speed menu offers Normal, Slow, Fast and Fastest. The original Help screen
may still describe PC keys.

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

With firmware V1.0.4, this edition saves to **`SAVES/MPE4-E92AE8A6.sav`**
on the SD card. The firmware creates `SAVES` automatically. Existing
`MPE4-E92AE8A6.sav` files in the SD root remain readable and are left intact;
the next successful save writes into `SAVES`.
The V1.0.2 cartridge retains the V1.0.1 game package and save identity.
Saves from the demo before the C64 menu corrections,
`MPE4-D6F947EB.sav`, remain separate: keep them for the earlier cartridge and
do not rename them to the new save filename.

## Source and credits

MPE/C64 adaptation, firmware and GUI: **John Swiderski â€” Mean Hamster Software**.
Learn about the compiler at [AGI-64](https://meanhamster.com/games/agi-64).

The Black Cauldron is Al Lowe's Sierra game based on Disney's film. The game
download comes from [Al Lowe's games page](https://allowe.com/downloads/games.html),
where he invites readers to copy the listed games and share them with friends.
The original game content retains its original ownership; this conversion
does not change those rights.

The cartridge uses [The Black Cauldron ZIP hosted by Al Lowe](https://allowe.com/download/The%20Black%20Cauldron.zip).
It retains the original startup and supplied resources, with the compiler's
C64 menu/key adaptation applied to LOGIC resources. The downloaded `VOL.2`
differs by one picture byte from the compiler's other Black Cauldron source
variant; this demo retains the downloaded picture unchanged. Input hashes,
the exact variant and compiler transformations are recorded in
[manifest.json](manifest.json).

## Build and verification

Built from AGI-64 revision `4ba9e4c75a01c5ae976d22f53848b9f5ca2218a6` with
sprites, mouse input and C64 menus enabled. The package contains 363 entries,
including the engine font. Checks cover the exact cartridge layout, resource
checksums and native SD loader, plus 1,362 frames through the actual integrated
firmware module: title, fresh Return into room 8, movement, help, menus,
object/action shortcuts and exact
bitmap/sprite publication. The exact cartridge terminal also replays the
published packets with injected raster interrupts. Input checks verify that
a pending event cannot be overwritten and consuming it leaves bus interrupts
enabled. These are host checks; a complete playthrough and
physical C64/TeensyROM+ acceptance remain unverified.

[SHA256SUMS.txt](SHA256SUMS.txt) identifies the exact cartridge bytes.

To rebuild with AGI-64 source access, use the revision above and copy its
`config/bc-64.json` to a temporary profile. Set `sourceDir` to the absolute
extracted `The Black Cauldron.1987` folder and set `expectedMd5["vol.2"]` to
`07a2594ebc5c5b043bcd5e4ef80448f5`; retain the other fingerprints. Then run:

```powershell
node host/build-mpe4-game-cartridge.mjs --config C:\Build\bc-demo.json --out C:\Build\bc-demo --name The-Black-Cauldron-MPE --mouse true --egoSprites true --c64Menus true
```

The builder requires Node.js and the checkout's `cartconv` tool. Distribute
the resulting `.crt`; raw images, resource packages and build intermediates
are not needed on the SD card.
