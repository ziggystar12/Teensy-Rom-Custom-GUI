# MPE Firmware V1.0.6

Download **[MPE_Firmware-V1.0.6.hex](MPE_Firmware-V1.0.6.hex)** for TeensyROM+
Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines the Custom GUI
and native MHS Power Engine, internally recorded as **native14**.

## Responsiveness fixes

- Opening, moving through and closing menus reuse the retained desktop image.
  They no longer recapture SD filenames or redraw the whole browser. The loaded
  SD fixture uses about six times fewer CPU instructions to open a menu.
- Dialog and control drawing leave music and mouse interrupts enabled. The
  interrupt also moves the visible pointer while the main drawing code is busy.
- The loading indicator publishes only its progress track, cutting that redraw's
  CPU work by about seven times while preserving its appearance.

These are software measurements; physical C64/Teensy timing remains to be tested.

## Desktop changes

- Shared bitmap controls now draw window frames, clear X close buttons,
  buttons, checkboxes and scrollbars across the desktop and built-in apps.
- A four-column, four-row file browser scrolls using arrows, a draggable thumb,
  keyboard or joystick. Open, Copy and Delete stay tied to the visible selection.
- Filenames preserve their case, dot and extension: `Text.txt`. Long labels
  use two lines and retain short extensions; dialogs show the full name.
- Firmware updates, loading, errors, file operations, autolaunch, KERNAL/REU
  assignments, hotkeys, mounted-disk confirmation and NFC prompts use the
  shared graphic dialog. Confirmation starts on Cancel and requires fresh input.
- Firmware confirmation captures its file and checks the target again before
  starting. The mouse remains active until an update is explicitly accepted.
- Text Viewer uses the same scrollbar and a 45-column, 17-line viewport.
  Dragging commits a new position on release, keeping file reads out of the
  pointer loop. It remains read-only.
- Icon selection publishes only affected pixels. Lowercase glyphs fit the
  existing font allocation, and all desktop/app memory bounds remain enforced.

Advanced Settings, Help and the compact recovery menu retain their separate
text interfaces. The shared desktop library and its current boundary are
documented in [UI-SYSTEM.md](../docs/UI-SYSTEM.md). View the
[native browser render](../docs/ui-preview/native-browser.png) and
[firmware dialog render](../docs/ui-preview/native-firmware.png), generated
from the assembled C64 drawing code with sample files.

## Controls and compatibility

F1 opens Help; F3 opens SD, F5 USB, F7 Teensy memory, and F8 Control Panel.
F6 opens Music. Open **TEENSY** for Snake, Calculator and Text Viewer.
File > Boot Disk, or Shift+RUN/STOP, boots the selected IEC Drive 8/9.
Plain RUN/STOP remains Back/Cancel in the desktop.

The V1.0.4 `/SAVES` behavior and separate SID tune/video/TOD labels are retained.
F5 saves in a game; F6 (Shift+F5) restores. The firmware creates `SAVES` on the
Teensy SD card. Restore checks that folder before trying older root saves;
existing root files remain untouched.

The cartridge and save-state formats are unchanged. Existing V1.0.2 game
cartridges, including the [Black Cauldron demo](../Demo/README.md), work with
this firmware and do not need rebuilding. The
[native10 transport corrections](../docs/NATIVE10-TRANSPORT.md) are retained.

Install the complete HEX using the [firmware guide](../docs/FIRMWARE-GUIDE.md).
Launch native cartridges from the **SD card**. Mouse: port 1. Joystick: port 2.
About identifies **MPE Firmware V1.0.6**, **John Swiderski**, and
**Mean Hamster Software**.

## Release record

Firmware SHA-256: `62df17725a131d50bdf326b51e877674c9f4bfbe31b0616203ad10f75da3b1db`

The [release manifest](../releases/native14/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image.
The [official restore image](../releases/native14/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older release kits remain available in the versioned release folders.

See [V1.0.6 validation](../docs/validation/MPE-V1.0.6.md) for executed desktop
checks, native game/save regression checks, and the final combined-image audit.
Software checks do not replace physical C64/Teensy timing tests; this firmware
has not been flashed here.

This folder contains only the current HEX and this README. Future releases
increment the final number (`V1.0.7`, `V1.0.8`, ...), using
[`firmware-version.json`](../firmware-version.json).

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
