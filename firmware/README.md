# MPE Firmware V1.0.4

Download **[MPE_Firmware-V1.0.4.hex](MPE_Firmware-V1.0.4.hex)** for TeensyROM+
Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines the Custom GUI
and native MHS Power Engine, internally recorded as **native12**.

- Game saves now go to **SAVES** on the Teensy SD card. The folder is created
  automatically; temporary and backup files stay there too. Existing root saves
  remain readable and untouched. F5 saves; F6 (Shift+F5) restores.
- Startup shows **SID tune timing** and **C64 video / TOD** separately. The default
  tune declares PAL timing independently of the machine. Clock detection and
  playback timing are unchanged.
- Desktop Help now fits above its footer and explains the built-in apps.
  Open the top-left **TEENSY** menu for **Snake**, **Calculator**, and
  **Text Viewer**. Text Viewer is read-only; a Notepad editor is not included.

The V1.0.3 desktop controls are retained:

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
The About panel shows **MPE Firmware V1.0.4**, **John Swiderski**, and
**Mean Hamster Software**.

The cartridge and save-state formats are unchanged. Existing V1.0.2 cartridges,
including the [Black Cauldron demo](../Demo/README.md), and their saves work with
this firmware; game cartridges do not need rebuilding. Restore checks the
new folder's save and backup before trying the same filenames in the SD root.
The [native10 transport corrections](../docs/NATIVE10-TRANSPORT.md) are retained.

Install the complete HEX using the [firmware guide](../docs/FIRMWARE-GUIDE.md).
Launch native cartridges from the **SD card**. Mouse: port 1. Joystick: port 2.

Firmware SHA-256: `9e6ff1860181e27a73a508849651e3141756cba18077e8a6b7e370978b1f502b`

The [release manifest](../releases/native12/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image.
The [official restore image](../releases/native12/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older release kits remain available in the versioned release folders.

See [V1.0.4 validation](../docs/validation/MPE-V1.0.4.md) for save failure and
compatibility checks, all 199 desktop checks, clock-label tests, and the final
combined-image audit. Software checks do not replace physical C64/Teensy timing
tests; this firmware has not been flashed here.

This folder contains only the current HEX and this README. Future releases
increment the final number (`V1.0.5`, `V1.0.6`, ...), using
[`firmware-version.json`](../firmware-version.json).

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
