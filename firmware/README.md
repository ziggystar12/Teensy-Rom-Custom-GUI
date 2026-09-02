# MPE Firmware V1.0.3

Download **[MPE_Firmware-V1.0.3.hex](MPE_Firmware-V1.0.3.hex)** for TeensyROM+
Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines the Custom GUI
and native MHS Power Engine, internally recorded as **native11**.

- F1 opens Help; F7 opens Teensy memory. The bundled Help describes the new controls.
- Icon selection updates the affected highlights; clicking an already selected
  icon no longer redraws the screen.
- File > Boot Disk, or Shift+RUN/STOP, uses `LOAD "*",8,1` or device 9 and starts
  the loaded program. Select Drive 8/9 or a disk folder/image in its IEC window.
  Teensy SD/USB images are not IEC drives. GEOS compatibility depends on the disk
  and drive. Plain RUN/STOP remains Back/Cancel in the desktop.
- F8 opens a nine-icon Control Panel with matching mouse targets, arrow-key
  navigation, and an X close button. Its original settings pages remain available.
- F6 opens Music: Browse, Play/Pause, Use Default, Autoplay, and Advanced. Open a
  `.sid`, then choose Use Default to save it as the background track.

The five-row browser, centered loading/error dialogs, desktop apps,
Copy/Paste/Delete, and confirmed firmware-update/recovery routes remain available.
The About panel shows **MPE Firmware V1.0.3**, **John Swiderski**, and
**Mean Hamster Software**.

The native AGI engine is unchanged from V1.0.2. Existing V1.0.2 cartridges,
including the [Black Cauldron demo](../Demo/README.md), and their saves work with
this firmware; game cartridges do not need rebuilding for these desktop changes.
The [native10 transport corrections](../docs/NATIVE10-TRANSPORT.md) are retained.

Install the complete HEX using the [firmware guide](../docs/FIRMWARE-GUIDE.md).
Launch native cartridges from the **SD card**. Mouse: port 1. Joystick: port 2.

Firmware SHA-256: `3ea79a98e6794a942e774e26d590b8fb836ad62384ccdb0804ee3f6899490a37`

The [release manifest](../releases/native11/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image.
The [official restore image](../releases/native11/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older release kits remain available in the versioned release folders.

See [desktop validation](../docs/validation/GUI-V1.0.3.md) for executed input and
loading checks, memory limits, and emulator captures. Software checks do not
replace physical C64/Teensy timing tests; this firmware has not been flashed here.

This folder contains only the current HEX and this README. Future releases
increment the final number (`V1.0.4`, `V1.0.5`, ...), using
[`firmware-version.json`](../firmware-version.json).

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
