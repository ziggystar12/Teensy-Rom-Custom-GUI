# MPE Firmware V1.0.13

Download **[MPE_Firmware-V1.0.13.hex](MPE_Firmware-V1.0.13.hex)** for a
TeensyROM+ Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines
the MHS desktop, its separate resident apps, and the native **MHS Power
Engine**. Its internal release id is **native21**.

## Power Engine memory on demand

The MHS Power Engine code stays in Teensy flash. Title playback, a running game,
the MPE2 compatibility path, and native DOS now share one 65,536-byte working
arena in RAM2 instead of reserving separate 64 KiB buffers.

The arena has an explicit owner. A native title claims it, then hands it directly
to the game engine. A normal game or MPE2 shutdown releases the ownership. DOS
claims the arena only after its files and hardware state pass preflight; once DOS
begins direct RAM2 execution, that ownership is intentionally reset-only. Failed
starts release the arena before returning to the desktop.

Link checks require one shared arena and reject duplicate native buffers or
insufficient stack/heap reserves. Exact linked memory usage is recorded with
the current build and validation evidence; older release addresses and sizes
are not assumed for this image.

## Desktop, storage, and updates

Menus and choice changes publish only the pixels they alter. Music and mouse
service remain active while foreground drawing runs. SD mounts are reused
across browsing, launching, transfers, and firmware checks. SD and USB listings
retain deterministic folder/file sorting and the 4,000-entry limit.

Copy `MPE_Firmware-V1.0.13.hex` to the Teensy SD root. If the installed GUI
rejects that unchanged file with “Firmware selection changed,” press **V**
and install it once through the original text menu. An older installed GUI
cannot receive this correction until the new firmware has been flashed.

V1.0.13 removes separate SD status/CMD13 probes during GUI HEX fingerprinting.
Those extra commands could fail and disturb the SDIO file stream. Exact file
identity, size, clean EOF, cancellation and CRC checks remain enforced. Startup
discovery still scans names and sizes first, offering the highest newer
version and deferring payload reads until confirmation. Opening or refreshing
SD retries discovery; manual selection remains available. The corrected GUI
update path requires physical acceptance on this exact image.

The update confirmation starts on Cancel and accepts only fresh input. After
Update is chosen, the desktop fingerprints the selected file and validates its
Intel HEX records before moving flash. STOP, a fresh click, or the preflight
timeout can cancel before the non-cancellable flash move begins. The updater
does not rename or delete the HEX file.

After reboot, open **TEENSY > About MPE Firmware** and confirm **V1.0.13**. The
panel credits **John Swiderski** and **Mean Hamster Software**, displays
`www.MeanHamster.Com`, and closes with the standard X used by other windows.

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

The native21 source record contains 47 ordered integration patches, one shared
native-runtime header, nine native game-engine sources, and 16 compiled native
DOS sources.

## DOSVM R17

The [DOSVM package](../dos/README.md) uses the same V1.0.13 firmware with its
matching R17 CRT and read-only FreeDOS disk. R16 ran Boulder well before an
unstable-packet error about ten seconds into play. Its fixed signature bytes
showed corrupted reads; this did not prove a publisher mutation.

R17 retains the direct-RAM speed changes. A failed read requests a quiet retry:
firmware finishes its current VM slice, pauses foreground execution, and
signals readiness to reread the same packet. Only the matching ACK resumes
normal execution. CRC validation and bounded retries still reject persistent
errors. Sustained R17 gameplay needs a new physical test.

## Release record

The [native21 manifest](../releases/native21/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) record the exact firmware size,
SHA-256 and build inputs. The
[official restore image](../releases/native21/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and earlier immutable release kits remain available under `releases/`.

See [V1.0.13 validation](../docs/validation/MPE-V1.0.13.md) and the
[installation guide](../docs/FIRMWARE-GUIDE.md). Build and deterministic test
results are separate from physical flashing, cold boot, GUI update, native
session and sustained DOS gameplay acceptance.

This folder contains only this README and the current firmware HEX. Future
releases increment the final number: V1.0.14, V1.0.15, and so on.

MHS Power Engine and AGI-64 by **John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
