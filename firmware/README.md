# MPE Firmware V1.0.19

Download **[MPE_Firmware-V1.0.19.hex](MPE_Firmware-V1.0.19.hex)** for a
TeensyROM+ Fab0.4 with Teensy 4.1 and a C64/128. This complete image combines
the MHS desktop, its on-demand utility apps, the native **MHS Power Engine**,
and **DOSVM**. Its internal release id is **native27**.

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

Copy `MPE_Firmware-V1.0.19.hex` to the Teensy SD root. If an older installed
GUI rejects that file with “Firmware selection changed,” press **V** and
install it once through the original text menu. An older installed GUI cannot
receive this correction until the new firmware has been flashed.

V1.0.19 retains the corrected GUI HEX fingerprinting without separate SD
status/CMD13 probes.
Those extra commands could fail and disturb the SDIO file stream. Exact file
identity, size, clean EOF, cancellation and CRC checks remain enforced. Startup
discovery still scans names and sizes first, offering the highest newer
version and deferring payload reads until confirmation. Opening or refreshing
SD retries discovery; manual selection remains available. The corrected GUI
update path worked on physical hardware with V1.0.15, as confirmed by the user.

The update confirmation starts on Cancel and accepts only fresh input. After
Update is chosen, the desktop fingerprints the selected file and validates its
Intel HEX records before moving flash. STOP, a fresh click, or the preflight
timeout can cancel before the non-cancellable flash move begins. The updater
does not rename or delete the HEX file.

After reboot, open **TEENSY > About MPE Firmware** and confirm **V1.0.19**. The
panel credits **John Swiderski** and **Mean Hamster Software**, displays
`www.MeanHamster.Com`, and closes with the standard X used by other windows.

## Desktop controls and apps

Home shows shortcuts for **F1 Help, F2 BASIC, F3 SD, F5 USB, F7 MEM, F8 PANEL,
and V TEXT**. The same keys work in browser windows, whose four-column,
five-row layout uses the space previously occupied by a bottom status strip.
Loading, file details, messages, errors, and confirmations use centered modal
boxes. Loading activity fills from left to right and restarts when its total is
unknown; file-copy progress uses its measured copy and verification state.

Dragging a desktop icon keeps a visible ghost under the pointer and shows its
snap grid. Control Panel > Appearance selects Light/Dark mode and
Dots/Dithered/Blank wallpaper. Input assigns Mouse or Joystick to either port;
two joysticks are allowed, while selecting the single supported mouse moves it
from the other port. Storage shows SD/USB identity, capacity, and free space,
plus total/free firmware flash information. F4 toggles SID play/pause, F6 opens
Music, and Control Panel > Startup > E saves the GUI/original-menu preference.

Snake, Calculator, and the read-only Text Viewer are separate C64 programs
bundled in this one HEX. Each streams into the shared `$C000` app space only
when launched; its close button or STOP returns to the desktop. The compact
original interface remains the boot/recovery path, while the native desktop,
settings, and apps occupy and reuse C64 memory at different times.

Advanced Settings and Help retain their dedicated text interfaces. Launch
native cartridges from the **SD card**.

## Game and save compatibility

V1.0.19/native27 includes every M4G2 AGI runtime change introduced by the
immutable V1.0.18/native26 release and requires M4G2 game cartridges. Fastest
no longer shares Fast's scheduler delay, compact predecoded ego VIEW sidecars
avoid recurring AGI RLE and mirror work, unchanged parser/status presentation
is not republished, and raw VIEW resources remain the checked fallback. Main
characters retain the four-layer VIC sprite path when the cartridge declares
it, with the legacy bitmap fallback preserved.

F5 saves and F6 restores. Each game has twelve stable manual slots at
`/SAVES/IIIIII01.SAV` through `/SAVES/IIIIII12.SAV`; replacement is validated
before promotion and the previous committed slot remains available as a backup.
M4G1 package-CRC saves remain separate rather than being silently migrated.

The native27 source record identifies the integration patches and exact
shared-runtime, native game-engine and native DOS inputs.

## DOSVM

The [DOSVM distribution](../DOSVM/README.md) contains the matching firmware
and cartridge, a fresh writable 20 MiB FAT16 C: image, and SD-folder files.
D: maps to `/DOSVM/D/`; games and saves there are ordinary files accessible
from your PC. Use DOS 8.3 names. `MEM`, `XCOPY`, `MORE` and `ATTRIB` are on PATH.

DOSVM is a working TeensyROM component, with CGA, PC-speaker sound, keyboard
input and port-2 joystick translation. It keeps all 512 KiB of direct RAM2
for the guest and uses spare RAM1 for the folder driver. The internal cartridge
revision paired with this firmware is R23; [hardware notes](../dos/HARDWARE-TEST.md)
record working Might and Magic, the successful V1.0.15 automatic firmware
update, and revision-specific checks. Scrolling
now repaints visibly; only a change between bitmap formats hides the screen
until its replacement is ready.

Press **Ctrl+Commodore+F7** in DOSVM to toggle sharp 320x200 CGA graphics.
The default remains the existing multicolour renderer. Sharp mode preserves
fine pixel detail with the C64 hires limit of two colours per 8x8 cell; it
changes only presentation and applies to CGA applications generally.
Upgrades retain their drives. Install the V1.0.19 firmware and paired R23 CRT;
run `D:\DOSVMUPD\UPDDOS` if the R20 startup update was not already applied.

Upgrades must preserve `/DOSVM/DOSVM.IMG` and `/DOSVM/D/`. Install the firmware
and CRT separately from the fresh disk template; follow the
[storage upgrade instructions](../dos/STORAGE.md#upgrading-dosvm) for startup
file updates. The Sierra runtime and quiet packet recovery remain included.

## Release record

The [native27 manifest](../releases/native27/manifest.json),
[source lock](../docs/firmware/source.lock.json), and
[checksums](../docs/firmware/SHA256SUMS.txt) record the exact firmware size,
SHA-256 and build inputs. The
[official restore image](../releases/native27/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex)
and earlier immutable release kits remain available under `releases/`.

The [native26 manifest](../releases/native26/manifest.json) remains the
immutable record for the V1.0.18 M4G2 AGI release whose runtime changes are
carried forward here. See the current native27 manifest and the
[installation guide](../docs/FIRMWARE-GUIDE.md). Build and deterministic test
results are separate from physical flashing, cold boot, GUI update, native
session and sustained DOS gameplay acceptance.

This folder contains only this README and the current firmware HEX. Future
releases increment the final number: V1.0.19, V1.0.20, and so on.

MHS Power Engine, AGI-64 and DOSVM integration by
**John Swiderski / Mean Hamster Software**.
[AGI-64 project information](https://meanhamster.com/games/agi-64).
