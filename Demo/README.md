# The Black Cauldron: MPE demo

[Download The-Black-Cauldron-MPE.crt](The-Black-Cauldron-MPE.crt?raw=true)
and play the supplied game on a C64/128 with TeensyROM+ Fab0.4, Teensy 4.1,
and the native MHS Power Engine firmware. No game compilation is needed.

## Start playing

1. Install the combined [native08 firmware](../releases/native08/MHS-PowerEngine-TRPlus-v1_full.hex?raw=true)
   if it is not already installed. Follow the [firmware installation guide](../releases/native08/MHS-POWER-ENGINE.md#install-the-custom-firmware).
   Native08 includes the desktop apps and Copy, Paste, and permanent Delete.
2. Copy `The-Black-Cauldron-MPE.crt` to the TeensyROM+ **SD card**. A folder
   named `Demo` on the card is fine.
3. Open that folder in the TeensyROM menu and launch the CRT. Release the
   launch key, then press **RETURN** when the game's title asks for a key.

Native game cartridges must launch from SD. The CRT requires native MPE
firmware; stock TeensyROM firmware and VICE cannot run its gameplay. Native07
also supports this cartridge, but native08 is the current combined release.

## Controls

Use the cursor keys or a joystick in **port 2** to move. Mouse support is
enabled in this cartridge; an optional 1351 mouse goes in **port 1**.

**RUN/STOP** opens the game's menus. Use cursor keys to navigate and **RETURN**
to select. The menus include Inventory and the game's object/action commands.
Menu labels show the C64 shortcuts; the original game's Help screen may still
describe PC function keys.

| Action | C64 shortcut |
|---|---|
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

On a C64, F2/F4/F6/F8 mean **Shift + F1/F3/F5/F7** respectively. Keep Shift
held for those even-numbered keys when also holding the Commodore key.

This edition saves to `MPE4-D6F947EB.sav` in the SD card's root directory.

## Source and credits

The Black Cauldron is Al Lowe's Sierra game based on Disney's film. The game
download comes from [Al Lowe's games page](https://allowe.com/downloads/games.html),
where he invites readers to copy the listed games and share them with friends.
The original game content retains its original ownership; this MPE conversion
does not change those rights.

This cartridge was compiled from
[The Black Cauldron ZIP hosted by Al Lowe](https://allowe.com/download/The%20Black%20Cauldron.zip).
It retains the supplied game's original startup and valid resources. The
download has a one-byte picture-data difference in `VOL.2` from the compiler's
existing Black Cauldron fingerprint; this build preserves the downloaded
version unchanged. The input hashes and precise difference are recorded in
[manifest.json](manifest.json).

## Build and verification

The cartridge was built with the native MPE builder from
[AGI-64 revision ac5f325](https://github.com/ziggystar12/AGI-64/commit/ac5f325050e0a9b8fb94bdea9fccffc225504f63).
The package contains 363 entries, including the engine font. Cartridge layout,
resource checksums, and the native08 SD loader were checked against the exact
distributed CRT. Native startup, input, and rendering checks are recorded in
the manifest. These are computer-based checks; a complete playthrough and
physical C64/TeensyROM+ gameplay have not been verified for this download.

[SHA256SUMS.txt](SHA256SUMS.txt) identifies the exact cartridge bytes.

To rebuild, download and extract the linked ZIP, check out the AGI-64 revision
above, and copy its `config/bc-64.json` to a temporary profile. Set `sourceDir`
to the absolute extracted `The Black Cauldron.1987` folder and set
`expectedMd5["vol.2"]` to `07a2594ebc5c5b043bcd5e4ef80448f5`; retain the other
fingerprints. From the AGI-64 checkout, run:

```powershell
node host/build-mpe4-game-cartridge.mjs --config C:\Build\bc-demo.json --out C:\Build\bc-demo --name The-Black-Cauldron-MPE --mouse true
```

The builder requires Node.js and the checkout's `cartconv` tool. Use a separate
build folder and distribute the resulting `.crt`; the intermediate raw image
and resource package are not needed on the SD card.
