# MPE Firmware V1.0.10

Download **[MPE_Firmware-V1.0.10.hex](MPE_Firmware-V1.0.10.hex)** for a
TeensyROM+ Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines
the MHS desktop, its separate resident apps, and the native MHS Power Engine.
Its internal release id is **native18**.

## Faster desktop and storage

- Menus and choice changes publish only the pixels they alter. Music and mouse
  service remain active while the foreground draws; the measured choice repaint
  fell from 476,772 to 99,867 emulated CPU cycles.
- SD mounts are reused across browsing, launching, transfers and NFC work. An
  empty socket avoids the multi-second mount path, and failed cards retry after
  a bounded delay or an explicit refresh.
- SD and USB directories use deterministic parent, folder, then file ordering.
  Pooled name storage and `O(n log n)` sorting keep the existing 4,000-entry
  limit responsive; the maximum fixture needed 62,074 comparisons.
- Firmware discovery scans SD-root filenames and sizes without reading the
  image before asking. A full CRC pass begins only after Update is chosen.
- Keyboard events and mouse-button transitions use ordered native-engine
  queues. Pointer motion and held joystick direction coalesce to their newest
  state. A full queue leaves the exact C64 event unacknowledged for retry.

The desktop code uses 22,506 of 22,528 bytes. The separate `GeosApps` payload
uses 4,093 of 4,096 bytes.

## Safer firmware updates

V1.0.9 can offer V1.0.10 automatically. Copy
`MPE_Firmware-V1.0.10.hex` to the Teensy SD root and start the GUI. The desktop
offers the highest strictly newer numeric version. Opening or refreshing SD
performs another bounded check after inserting or changing a card.

Users upgrading directly from V1.0.7 or V1.0.8 should select V1.0.10 manually;
those versions can miss an SD card during cold startup. Manual selection works
from SD or USB.

The confirmation starts on Cancel and accepts only fresh input. After Update is
chosen, the desktop fingerprints the exact path and contents. The parser reads
the file again while staging and rejects replacement, truncation, malformed
records, missing EOF, trailing data, or out-of-range addresses before moving
flash. STOP, a fresh click, or the 29-second preflight timeout can cancel before
the non-cancellable flash move begins. The updater never renames or deletes the
HEX file.

After reboot, open **TEENSY > About MPE Firmware** and confirm **V1.0.10**.
The panel credits **John Swiderski** and **Mean Hamster Software**.

## Desktop controls and apps

Home and browser windows share these shortcuts: **F1 Help, F2 BASIC, F3 SD,
F5 USB, F7 MEM, F8 PANEL, and V TEXT**. F4 toggles SID play/pause and F6 opens
Music. V switches between the bitmap GUI and the original text menu; Control
Panel > Startup > E saves that preference.

Snake, Calculator, and the current read-only Text Viewer/Notepad app live in
the separate resident `GeosApps` payload. The desktop core only launches them
and provides shared drawing, input, and file services. Their close button or
STOP returns to the desktop without resetting the cartridge.

Advanced Settings, Help, and the compact recovery menu retain their dedicated
text interfaces. Launch native cartridges from the **SD card**. Mouse: port 1.
Joystick: port 2.

## Native engine compatibility

The game cartridge and save formats are unchanged. Existing V1.0.2 or later
MPE game cartridges, including the [Black Cauldron demo](../Demo/README.md), do
not need rebuilding. Main characters retain the four-layer VIC sprite path
when the cartridge declares it, with the legacy bitmap fallback preserved.

F5 saves a game and F6 restores it. Saves remain under
`/SAVES/MPE4-XXXXXXXX.sav` on the Teensy SD card. Existing root saves remain
read-only restore fallbacks.

This release also carries the current native DOS engine and the 45-patch
integration chain. The manifest records nine native AGI sources and 16 compiled
native DOS sources separately.

## Release record

Firmware SHA-256:
`611a38b72e5fc8521dcab4bcbe465dfd18ed95cd684082a7ed25c93a5f8d44cd`

The [native18 manifest](../releases/native18/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image. The
[official restore image](../releases/native18/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older immutable release kits remain available under `releases/`.

See [V1.0.10 validation](../docs/validation/MPE-V1.0.10.md) and the
[installation guide](../docs/FIRMWARE-GUIDE.md). Software and emulator checks
do not replace testing a flash, update, storage changes, mouse/SID continuity,
or native sessions on physical C64/TeensyROM hardware.

This folder contains only this README and the current firmware HEX. Future
releases increment the final number: V1.0.11, V1.0.12, and so on.

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
