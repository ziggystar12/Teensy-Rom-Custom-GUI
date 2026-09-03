# MPE Firmware V1.0.11

Download **[MPE_Firmware-V1.0.11.hex](MPE_Firmware-V1.0.11.hex)** for a
TeensyROM+ Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines
the MHS desktop, its separate resident apps, and the native **MHS Power
Engine**. Its internal release id is **native19**.

## Power Engine memory on demand

The MHS Power Engine code stays in Teensy flash. Title playback, a running game,
the MPE2 compatibility path, and native DOS now share one 65,536-byte working
arena in RAM2 instead of reserving separate 64 KiB buffers.

The arena has an explicit owner. A native title claims it, then hands it directly
to the game engine. A normal game or MPE2 shutdown releases the ownership. DOS
claims the arena only after its files and hardware state pass preflight; once DOS
begins direct RAM2 execution, that ownership is intentionally reset-only. Failed
starts release the arena before returning to the desktop.

The final linked image contains one `MHSNativeArenaStorage` at `0x20206320` and
one 16-byte ownership record at `0x20061CF4`. The former duplicate
`MPEVirtualRAM` and `MPE3TitleInternalAssets` allocations are absent. MinimalBoot
now retains 337,376 bytes of RAM2 heap reserve, exactly 65,536 bytes more than
V1.0.10, while retaining 21,408 bytes of stack reserve.

## Desktop, storage, and updates

Menus and choice changes publish only the pixels they alter. Music and mouse
service remain active while foreground drawing runs. SD mounts are reused
across browsing, launching, transfers, and firmware checks. SD and USB listings
retain deterministic folder/file sorting and the 4,000-entry limit.

V1.0.9 and V1.0.10 can offer V1.0.11 automatically. Copy
`MPE_Firmware-V1.0.11.hex` to the Teensy SD root and start the GUI. Opening or
refreshing SD performs another bounded check after inserting or changing a
card. Users upgrading directly from V1.0.7 or V1.0.8 should select V1.0.11
manually because those versions can miss an SD card during cold startup.

The update confirmation starts on Cancel and accepts only fresh input. After
Update is chosen, the desktop fingerprints the selected file and validates its
Intel HEX records before moving flash. STOP, a fresh click, or the preflight
timeout can cancel before the non-cancellable flash move begins. The updater
does not rename or delete the HEX file.

After reboot, open **TEENSY > About MPE Firmware** and confirm **V1.0.11**. The
panel credits **John Swiderski** and **Mean Hamster Software**.

## Desktop controls and apps

Home and browser windows share these shortcuts: **F1 Help, F2 BASIC, F3 SD,
F5 USB, F7 MEM, F8 PANEL, and V TEXT**. F4 toggles SID play/pause and F6 opens
Music. V switches between the bitmap GUI and the original text menu; Control
Panel > Startup > E saves that preference.

Snake, Calculator, and the current read-only Text Viewer/Notepad app live in
the separate resident `GeosApps` payload. The desktop core launches them and
provides shared drawing, input, and file services. Their close button or STOP
returns to the desktop without resetting the cartridge.

Advanced Settings, Help, and the compact recovery menu retain their dedicated
text interfaces. Launch native cartridges from the **SD card**. Mouse: port 1.
Joystick: port 2.

## Game and save compatibility

The MHS Power Engine cartridge and save formats are unchanged. Existing V1.0.2
or later MPE game cartridges, including the
[Black Cauldron demo](../Demo/README.md), do not need rebuilding. Main
characters retain the four-layer VIC sprite path when the cartridge declares
it, with the legacy bitmap fallback preserved.

F5 saves a game and F6 restores it. Saves remain under
`/SAVES/MPE4-XXXXXXXX.sav` on the Teensy SD card. Existing root saves remain
read-only restore fallbacks. Keyboard, joystick, and mouse input contracts are
unchanged.

The native19 source record contains 46 ordered integration patches, one shared
native-runtime header, nine native game-engine sources, and 16 compiled native
DOS sources.

## Release record

`MPE_Firmware-V1.0.11.hex` is 6,336,742 bytes. Its SHA-256 is:

`87c1680a4056a3addda694dbdf0d875b8fe56b2c72cc4e35e5559674fd0ae3d5`

The [native19 manifest](../releases/native19/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) identify the exact image. The
[official restore image](../releases/native19/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and older immutable release kits remain available under `releases/`.

See [V1.0.11 validation](../docs/validation/MPE-V1.0.11.md) and the
[installation guide](../docs/FIRMWARE-GUIDE.md). Software and deterministic
host checks pass. Flashing, cold boot, update behavior, mouse/SID continuity,
native game sessions, and native DOS still require final acceptance on physical
C64/TeensyROM hardware.

This folder contains only this README and the current firmware HEX. Future
releases increment the final number: V1.0.12, V1.0.13, and so on.

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
