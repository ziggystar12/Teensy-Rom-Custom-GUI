# MHS Power Engine native AGI firmware kit

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

- `MHS-PowerEngine-TRPlus-v1_full.hex`: matching native MHS firmware.
- `TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex`: pinned official restore image.
- `MHS-POWER-ENGINE.md`: this guide.
- `SHA256SUMS.txt`: hashes of the exact files in this kit.

The compiler checks the custom and restore images against its pinned hashes
before exporting a cartridge. Use this kit's checksums when checking your copy.
Do not substitute an older MPE picture-acceleration or test firmware image.

## Install the custom firmware

1. Power off the C64/128, attach TeensyROM+, insert the storage containing the
   kit, and power on.
2. In the TeensyROM menu, select `MHS-PowerEngine-TRPlus-v1_full.hex`.
3. Check the entire filename and press `Y` to confirm.
4. Keep the C64/128 powered during erase and programming. Wait for the
   automatic reboot before resetting or removing the cartridge.
5. Confirm the TeensyROM menu opens, then launch the matching native CRT.

The custom image includes the selected TeensyROM custom GUI. The upper/full
firmware retains its network features. MinimalBoot disables TCP Listen during
large-cartridge sessions to reserve working memory for the engine.

The native08 build pairs Custom GUI revision
`ac4a5d6ce3d8037d4fdd7eee58899b9bc7463b3e` with the native07 AGI engine.
It includes the desktop apps and SD/USB file operations described below, and
retains the corrected waits for a new key, including the King's full speech
in KQ1. Existing native06 and native07 cartridges and per-game saves remain
compatible; update the firmware without rebuilding cartridges. The native05,
native06 and native07 releases remain separate rollbacks. Use the release
manifest and checksums for the final combined image and its verification record.

The SD save filename is `/MPE4-XXXXXXXX.sav`, with the eight-digit package
CRC32 shown in the game build report. Old `/MPE4-SQ1.sav` files are preserved;
they are not migrated. Valid native05 per-game saves retain their original
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
