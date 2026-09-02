# MPE Firmware V1.0.1

Download **[MPE_Firmware-V1.0.1.hex](MPE_Firmware-V1.0.1.hex)** for TeensyROM+
Fab0.4 with Teensy 4.1 and a C64/128. This is the combined Custom GUI and native
MHS Power Engine release, internally recorded as **native09**.

This release restores the main character's four hardware sprite layers in newly
built games, including KQ1/KQ2 eye details and correct scenery/dialog masking.
Mouse menu clicks survive game restart and restore. Rebuilt cartridges use the
regular C64 menu shortcuts, with Commodore combinations for extra game actions.
The high-resolution command line and corrected dialogue key waits are retained.

The desktop includes the animated Loading panel, corrected parent-folder
navigation, desktop apps, Copy/Paste/Delete, and an About panel showing
**MPE Firmware V1.0.1**, **John Swiderski**, and **Mean Hamster Software**.

Install the complete HEX using the [firmware guide](../docs/FIRMWARE-GUIDE.md).
Launch native game cartridges from the **SD card**. Mouse: port 1. Joystick:
port 2. RUN/STOP opens game menus. Install this firmware before launching the
new sprite cartridges. Older cartridges still run using their bitmap renderer.

The new menu adaptation changes rebuilt cartridges' save filenames. Keep older
saves with their matching older cartridges; do not rename them to the new files.
The new filenames are listed with each game build and in the
[Black Cauldron demo](../Demo/README.md).

Firmware SHA-256:

`6f23f596491dfa6d1601f2e0a3a27c56677d875ba7d95d592551b25e869234de`

The [release manifest](../releases/native09/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image.
The [official restore image](../releases/native09/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older releases remain available in the versioned release folders.

The final combined-image audit, 173 desktop/backend checks, and native engine
tests passed. The integrated run covered 732 gameplay frames and 285 input
events, including sprite publication and save/restore. Both linked firmware
components and the embedded GUI match their recorded sources. This release has
not been flashed or physically tested here; hardware speed and full game
playthroughs remain to be checked.

This folder contains only the current HEX and this README. Future releases
increment the final number (`V1.0.2`, `V1.0.3`, ...), using
[`firmware-version.json`](../firmware-version.json); replace the current HEX here
and preserve release kits under `releases/`.

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
See the [project README](../README.md) for upstream credits and build instructions.
