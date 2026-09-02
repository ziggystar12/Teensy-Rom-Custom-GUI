# MPE Firmware V1.0.4 native AGI kit

This kit accompanies a cartridge built with **MHS Power Engine (native AGI)**
in the AGI-64 Compiler. Keep the cartridge and its matching kit together.
Firmware is separate from the CRT. The compiler never flashes hardware.

## Compatibility

- TeensyROM+ Fab0.4 with a Teensy 4.1 and the matching custom firmware.
- Native CRTs on the SD card. Internal flash and USB storage cannot launch a
  native session.
- Small games retain the 1 MiB EasyFlash boot layout. Larger games use an
  MPE-specific extension up to 4 MiB, with additional resource banks read only
  by Teensy. Bank 58 remains reserved for the engine.
- AGI resource packages, including supported normalized AGI v3 game data.
- Original game title and resources. C64 picture compression, display profiles,
  character caches, and optimization switches do not apply to native builds.
- Keyboard and joystick controls, plus optional 1351 mouse support selected
  when building the cartridge.

The native engine runs AGI logic, picture and actor rendering, collision checks,
parser handling, and game state on the Teensy. The C64 presents the resulting
frames and SID sound and collects input. The CRT contains the game resources;
the firmware does not contain a particular game. Native CRTs do not contain a
complete C64 game fallback. Stock firmware and VICE cannot run native gameplay.

SQ1 keeps its complete introduction and in-game skip control. Other supported
sources begin their own original LOGIC 0 through a short neutral startup.
The supplied game bytecode is retained. Each game has its own save slot,
selected by the packaged game identity so one game does not overwrite another.

A successful compiler build verifies the source fingerprint, resource package,
cartridge mapping, output hashes, and matching firmware. It does not establish
that every game can be played through its ending. Keep build verification,
emulator boot checks, and physical gameplay results separate.

## Kit contents

- `MPE_Firmware-V1.0.4.hex`: matching native MHS firmware.
- `TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex`: pinned official restore image.
- `MHS-POWER-ENGINE.md`: this guide.
- `SHA256SUMS.txt`: hashes of the exact files in this kit.

The compiler checks the custom and restore images against its pinned hashes
before exporting a cartridge. Use this kit's checksums when checking your copy.
Do not substitute an older MPE picture-acceleration or test firmware image.

## Install the custom firmware

1. Power off the C64/128, attach TeensyROM+, insert the storage containing the
   kit, and power on.
2. In the TeensyROM menu, select `MPE_Firmware-V1.0.4.hex`.
3. Check the entire filename and press `Y` to confirm.
4. Keep the C64/128 powered during erase and programming. Wait for the
   automatic reboot before resetting or removing the cartridge.
5. Confirm the TeensyROM menu opens, then launch the matching native CRT.

The custom image includes the selected TeensyROM custom GUI. The upper/full
firmware retains its network features. MinimalBoot disables TCP Listen during
large-cartridge sessions to reserve working memory for the engine.

V1.0.4 uses the internal release id `native12`; the release manifest records
the exact selected GUI revision. It includes the desktop apps,
SD/USB file operations, a 25-icon browser, centered loading/message dialogs,
and parent navigation through
the up control instead of a synthetic `/..` desktop item.
Open **TEENSY > About MPE Firmware** to
check the installed version and the credits for **John Swiderski** and
**Mean Hamster Software**.

Use the rebuilt game cartridges with V1.0.1 or later to enable the four-layer sprite
display for the main character. Older packages retain their original display
mode. The native05 through native11 releases remain separate rollbacks. Use
the release manifest and checksums for the exact combined image and its
verification record.

## Game saves

F5 saves the current game; F6 (Shift+F5 on a C64) restores it. Each packaged
game has one slot, also accessible through its Save/Restore menu actions.

V1.0.4 writes `/SAVES/MPE4-XXXXXXXX.sav` on the Teensy SD card, creating
`SAVES` on the first save. The eight-digit package CRC32 is shown in the game
build report. Temporary files and the preceding save's `.bak` stay in that
folder too. A failed folder creation or a regular file named `SAVES` produces
a save error; the firmware never falls back to writing files in the root.

Restore tries the folder's `.sav`, then its `.bak`, before trying the same
filenames in the SD root for older firmware saves. Existing root files remain
untouched; the next successful save goes into `SAVES`. Current cartridges and
save identities are unchanged, so game cartridges do not need rebuilding.
Old `/MPE4-SQ1.sav` files remain separate and are not migrated.
Valid native05 per-game saves retain their original
state and receive an empty new key-binding area when loaded by native06 or later.
Package identity, file length and both checksums are checked before restoration.

## Desktop file operations

In an SD or USB folder, select an individual file and use Edit > Copy, then
Edit > Paste in the destination folder. The keyboard shortcuts are Shift+C
and Shift+P. Paste can copy between SD and USB, verifies the copy before
publishing it, and refuses an existing destination filename.

File > Delete or Shift+D shows the selected filename and asks for permanent
deletion, with Cancel selected initially. There is no Trash or recovery store.
Folder operations, files inside disk images, and IEC Drive 8/9 writes are not
supported. Native game cartridges still launch from SD only.

## Desktop controls and music

F1 opens Help; F3 opens SD, F5 USB, and F7 Teensy memory. F8 opens the icon-based
Control Panel. Arrows select a category; Return/fire opens it. Click its icon or
label to select, then click again to open. X, STOP, HOME, Escape, or F8 closes the
panel. The original settings categories retain their existing keyboard pages.

F6 opens Music. Choose Browse, open a `.sid`, then choose Use Default to save it
as the background track. Play/Pause changes playback now; Autoplay changes the
startup preference. Advanced retains subsong, speed, and voice controls.

File > Boot Disk or Shift+RUN/STOP performs `LOAD "*",8,1` (or device 9) and starts
the loaded program. Select a desktop Drive 8/9 icon or use its IEC directory
window. A selected disk folder/image is entered first; an ordinary file selection
boots the current disk. Teensy SD/USB image browsing is not an IEC drive. GEOS
compatibility depends on the disk's boot file and the attached drive/device.
Plain RUN/STOP remains Back/Cancel in the desktop.

Open the top-left **TEENSY** menu for **Snake**, **Calculator**, and
**Text Viewer**. Text Viewer is read-only; it is not a Notepad editor.
The Games/Utilities desktop folders are separate from these built-in apps.

The startup music message separates **SID tune timing** from **C64 video**
and **TOD**. The default tune declares PAL timing even on an NTSC C64;
that tune label does not select MPE game speed. The hardware line reports
the independently detected machine clocks. V1.0.4 changes these labels,
not clock detection or playback timing.

## Physical checks

Record the machine's video standard and the exact CRT and firmware hashes.
Check introduction progression, music, name entry, walking, room transitions,
menus and dialog dismissal, parser input, save/restore, and return to the
TeensyROM menu. If mouse support was selected, also check pointer motion,
click-to-walk, and menu selection. Note the room and action if anything fails.

## Restore official firmware

From a working TeensyROM menu, select
`TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex` and follow the same confirmed update
sequence above.

If the menu cannot boot, connect TeensyROM+ to a computer with a USB
A-to-micro-B cable while it remains installed in a powered C64/128. Open the
restore `.hex` in PJRC Teensy Loader and press the white program button on the
Teensy module. Keep the C64/128 powered until programming and reboot finish.
