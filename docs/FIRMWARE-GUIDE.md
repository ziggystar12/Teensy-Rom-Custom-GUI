# TeensyROM firmware V1.0.17

This firmware includes the TeensyROM GUI, MHS Power Engine and DOSVM.
MPE runs compatible AGI game data natively on Teensy. DOSVM runs FreeDOS
applications with CGA graphics, PC-speaker sound and writable C:/D: storage.
Firmware is separate from cartridges; the AGI-64 Compiler never flashes it.

Install DOSVM from the firmware repository's `DOSVM/` package and follow
`dos/README.md`. Existing users should follow `dos/STORAGE.md` to update the
firmware, cartridge and startup files while retaining their C: image and D:
files. DOSVM is confirmed working on hardware; `dos/HARDWARE-TEST.md` records
the successful V1.0.15 BIOS startup and Might and Magic launch, along with
checks for the current revision.

V1.0.17 adds the DOSVM black-and-white 80-column console, held BIOS-style
POST page, short beep and blinking text cursor. **Ctrl+Commodore+F7** retains
optional sharp 320x200 CGA rendering: it switches between that view and the
default multicolour display. Hires keeps fine pixel detail but limits each
8x8 cell to two colours. Guest video modes and game logic are unchanged; see
`dos/README.md` for display details. V1.0.15
users install the paired R23 CRT while retaining their C: image and D: files.
Users who already installed V1.0.17 only replace the CRT; run
`D:\DOSVMUPD\UPDDOS` if the R20 startup update was not already applied.

V1.0.17 retains the corrected GUI firmware updater and quiet packet recovery.
The user confirmed automatic firmware updating worked with V1.0.15.
If an older GUI rejects an unchanged HEX, press **V** and install through the
original text updater once. Keep MPE cartridges with their matching compiler
kit; the compatibility and game-save instructions below apply to those games.

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

## Power Engine compiler kit contents

- `MPE_Firmware-V1.0.17.hex`: matching MHS Power Engine firmware.
- `TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex`: pinned official restore image.
- `MHS-POWER-ENGINE.md`: this guide.
- `SHA256SUMS.txt`: hashes of the exact files in this kit.

The compiler checks the custom and restore images against its pinned hashes
before exporting a cartridge. Use this kit's checksums when checking your copy.
Do not substitute an older MPE picture-acceleration or test firmware image.

## Reproduce the release source

In the firmware repository, `docs/firmware/source.lock.json` names the exact
`engineCommit` containing the release and its build tools. Create a detached
worktree at that commit before following the repository's build instructions:

```powershell
$releaseSource = Get-Content docs/firmware/source.lock.json -Raw | ConvertFrom-Json
git worktree add --detach ../mpe-release-rebuild $releaseSource.engineCommit
Set-Location ../mpe-release-rebuild
```

V1.0.17 uses the verified 47-patch combined build. Its manifest records all
MHS Power Engine game-runtime sources, native DOS sources, and the shared
native runtime source separately. The release check
verifies each build tool against the locked commit's exact Git bytes, alongside
the firmware, patch chain, backend and selected GUI hashes.

## Install the custom firmware

When upgrading from V1.0.7 or V1.0.8, use one-time manual selection. V1.0.9 and
V1.0.10 can recognize a correctly named newer image in the SD-card root, but
they read and fingerprint the whole image before showing the prompt. Physical
V1.0.9 hardware has missed that prompt. If it does not appear after boot, open
SD and allow the scan to finish; if it remains absent, select the file manually.
V1.0.11 and later defer the payload CRC until confirmation and have stronger SD
mount settling and bounded retry. Automatic updating was confirmed working with V1.0.15; manual selection
is always available.

1. Power off the C64/128, attach TeensyROM+, insert the storage containing the
   kit, and power on.
2. In the TeensyROM menu, open SD or USB and select `MPE_Firmware-V1.0.17.hex`.
   If the older GUI reports a changed selection, switch to the original text
   menu with V and choose the same file there.
3. Check the entire filename and click Update or press `Y` to confirm. The
   bitmap dialog starts on Cancel; Return initially cancels. A changed source,
   folder or selection invalidates confirmation and requires choosing it again.
4. Keep the C64/128 powered during erase and programming. Wait for the
   automatic reboot before resetting or removing the cartridge.
5. After reboot, open TEENSY > About MPE Firmware and confirm V1.0.17.
   Update progress is drawn by the previous desktop until reboot. If an old
   version remains in About, restart the C64/Teensy before testing the new UI.
6. Launch the matching native CRT.

### Choose the startup menu

Open F8 Control Panel > Startup and press E to choose GUI or Text / Original.
The choice is saved automatically. V switches between the desktop and original
text menu and saves that choice too. Original text remains available on reboot;
press V there to return to the GUI. Help, Settings and BASIC are explicit launches
and do not change the saved preference.

Home and file windows share one clickable shortcut strip: F1 Help, F2 BASIC,
F3 SD, F5 USB, F7 MEM (Teensy memory), F8 PANEL and V TEXT. Press the key or
click its label. F4 controls SID pause/play; F6 opens the Music tools.

### Future updates from the SD card

After installing V1.0.17, copy a newer `MPE_Firmware-Vx.y.z.hex` to the root of
the Teensy SD card and start the GUI desktop. It offers the highest newer
numeric version in the shared firmware dialog. For example, V1.0.17 is newer
than V1.0.16. The filename must have three numeric components without leading
zeroes, suffixes or extra extensions; matching is case-insensitive. Installed
and older versions, directories and restore images are ignored.

The root name for this release is exactly `MPE_Firmware-V1.0.17.hex`. V1.0.9
and V1.0.10 use the older full-image startup check; open SD and allow that scan
to finish before concluding that no offer appeared. V1.0.11 and later use the
current deferred-CRC detector. If any version does not prompt automatically on
physical hardware, select the file manually and report the installed version,
candidate filename, and card state.

Check the displayed filename, then choose Update or press `Y`. Return initially
cancels. Cancel keeps the file; opening or refreshing SD can offer it again.
No file is installed merely because it was found.

Cold SD detection waits for its input to settle before reading it. A live mount
is reused, and an empty socket never enters the multi-second mount path. Startup
discovery enumerates names and sizes only; it reads no firmware payload before
showing the confirmation. Open SD using F3, the SD icon or the SD menu action to
refresh the directory and repeat the optional check after inserting or changing
a card. A failed card retries after a bounded delay as well. With Text / Original
saved as the startup mode, press V to enter the GUI before using discovery.

The file is not renamed or deleted. Once the newer version is installed, its
version number prevents another offer for the same file. The discovery scan
leaves your browser location and selection unchanged. After confirmation, one
full CRC pass binds the exact captured SD or USB file to the parser; the parser
recomputes that CRC while staging and rejects removal, replacement, malformed
records, out-of-range addresses, missing EOF, or trailing data before flash is
moved. STOP, a fresh click, or the bounded preflight timeout cancels before the
non-cancellable flash move begins. Manual `.hex` selection uses the same CRC
binding and remains available for recovery and downgrades.

V1.0.17 fingerprints the file without separate SD status probes during the
read. Those probes could interrupt an active SD stream and falsely invalidate
an unchanged selection. Opening, file size, exact EOF, cancellation and CRC
checks remain enforced; an actual read error still rejects the image.

The custom image includes the selected TeensyROM custom GUI. The upper/full
firmware retains its network features. MinimalBoot disables TCP Listen during
large-cartridge sessions to reserve working memory for the engine.

V1.0.17 uses the internal release id `native25`; the release manifest records
the exact selected GUI revision, nine MHS Power Engine game-runtime sources,
the native DOS sources, one shared native runtime source, and the 47-patch
integration chain. SD/USB file operations use cached
mounts. Directory names use pooled storage and deterministic parent/folder/file
sorting through the 4,000-entry limit. Dropdowns reuse the retained background;
choice changes and other bounded regions publish only their affected pixels.
Dialog and control drawing keeps music and mouse service active.
Open **TEENSY > About MPE Firmware** to
check the installed version and the credits for **John Swiderski** and
**Mean Hamster Software**. The panel also shows `www.MeanHamster.Com` and closes
with the same standard X used by other desktop windows; it has no separate close
instructions.

## Power Engine memory lifecycle

MHS Power Engine code remains in Teensy flash and is available whenever a
compatible cartridge launches. Its game workspace is constructed only when
needed. Title playback, an active Power Engine game session, legacy MPE2
compatibility, and native DOS share one 64 KiB RAM2 arena instead of reserving
separate large buffers. The title hands its arena ownership directly to Power
Engine at game launch.

Title, Power Engine, and MPE2 release the arena on a clean exit or reset so the
next mode can reuse it. Native DOS performs preflight first, then seals the
arena for reset-only direct execution. After DOS takes ownership, reset the
TeensyROM+ before launching another arena owner.

Use the rebuilt game cartridges with V1.0.1 or later to enable the four-layer sprite
display for the main character. Older packages retain their original display
mode. Earlier releases remain separate rollbacks. Use
the release manifest and checksums for the exact combined image and its
verification record.

## Game saves

F5 saves the current game; F6 (Shift+F5 on a C64) restores it. Each packaged
game has one slot, also accessible through its Save/Restore menu actions.

V1.0.17 writes `/SAVES/MPE4-XXXXXXXX.sav` on the Teensy SD card, creating
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

F1 opens Help; F2 exits to BASIC; F3 opens SD, F5 USB, and F7 Teensy memory.
These keys and V are also clickable on Home and file-window footers.
F8 opens the icon-based
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
**Text Viewer**. They are separate programs in the resident `GeosApps` payload;
the core desktop only launches them and supplies shared drawing/input services.
Text Viewer is read-only; it is not a Notepad editor. The Games/Utilities
desktop folders are separate from these apps.
The file browser and Text Viewer use draggable scrollbars. Cursor Up/Down
scrolls text one line; Left/Right moves one screen. Text is wrapped to 45
columns, with a bounded initial count and no file reads during thumb dragging.
Filenames preserve their case, dot and extension, for example `Text.txt`.

The startup music message separates **SID tune timing** from **C64 video**
and **TOD**. The default tune declares PAL timing even on an NTSC C64;
that tune label does not select MPE game speed. The hardware line reports
the independently detected machine clocks. The label clarification introduced
in V1.0.4 is retained; clock detection and playback timing are unchanged.

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
