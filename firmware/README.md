# MPE Firmware V1.0.2

Download **[MPE_Firmware-V1.0.2.hex](MPE_Firmware-V1.0.2.hex)** for TeensyROM+
Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines the Custom GUI
and native MHS Power Engine, internally recorded as **native10**.

V1.0.2 removes an interrupt pause from native input handling that could delay
the C64 bus service. Rebuilt game cartridges also retry transient packet-header
corruption before reporting an error, and disable game sprites on the diagnostic
screen. Install **both this firmware and the rebuilt cartridges** for the complete
correction. See the [transport notes](../docs/NATIVE10-TRANSPORT.md).

The desktop now shows five rows of five browser icons, with loading activity,
messages, and errors contained in centered dialogs. Its About panel identifies
**MPE Firmware V1.0.2**, **John Swiderski**, and **Mean Hamster Software**.
Desktop apps, Copy/Paste/Delete, parent-folder navigation, and the confirmed
firmware-update and recovery routes remain available.

The four hardware sprite layers, scenery/dialog masking, high-resolution command
line, C64 function-key controls, and mouse restart fixes from V1.0.1 are retained.
**V1.0.1 saves remain compatible with the rebuilt V1.0.2 cartridges.** The earlier
V1.0.1 menu adaptation changed package identities; keep pre-V1.0.1 saves with
their matching cartridges instead of renaming them.

Install the complete HEX using the [firmware guide](../docs/FIRMWARE-GUIDE.md).
Launch native game cartridges from the **SD card**. Mouse: port 1. Joystick:
port 2. RUN/STOP opens game menus. The refreshed
[Black Cauldron demo](../Demo/README.md) includes the new cartridge receiver.

Firmware SHA-256:

`49d41fcbae2b591b64a6d846d1f90c23e4fbf677405bcc87ff7a25f1b1ac5560`

The [release manifest](../releases/native10/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image.
The [official restore image](../releases/native10/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older releases remain available in the versioned release folders.

The final combined-image audit and all 179 desktop/backend checks passed.
The native firmware run covered 862 gameplay frames, 350 accepted inputs,
350 rejected competing input writes, and 64 direction reversals without masking
interrupts. All 1,184 output packets matched the rebuilt C64 receiver's display
and sprite state. A separate KQ1 receiver replay checked over 15,000 frames from
512 direction reversals. These are software checks; this firmware has not been
flashed here. Physical C64/Teensy timing still needs hardware confirmation.

This folder contains only the current HEX and this README. Future releases
increment the final number (`V1.0.3`, `V1.0.4`, ...), using
[`firmware-version.json`](../firmware-version.json); replace the current HEX here
and preserve release kits under `releases/`.

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
See the [project README](../README.md) for upstream credits and build instructions.
