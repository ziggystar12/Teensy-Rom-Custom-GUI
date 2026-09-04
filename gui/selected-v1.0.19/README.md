# TeensyROM: GUI, MHS Power Engine and DOSVM

![TeensyROM native scrolling desktop](docs/ui-preview/native-browser.png)

TeensyROM combines a desktop GUI, the native **MHS Power Engine**, and
**DOSVM** in one firmware project for **TeensyROM+ Fab0.4 with a Teensy 4.1**.
The GUI provides mouse, joystick and keyboard operation. MPE runs AGI games
on the Teensy, and DOSVM runs FreeDOS applications with writable SD storage,
CGA graphics and PC-speaker sound. The C64 supplies the display and controls.
Additional emulators are planned.

## Download and start

1. Download [MPE Firmware V1.0.19](firmware/MPE_Firmware-V1.0.19.hex?raw=true)
   and follow the [installation guide](docs/FIRMWARE-GUIDE.md#install-the-custom-firmware).
   This complete image includes the desktop, its apps, Copy/Paste/Delete, and
   the MHS Power Engine and DOSVM.
2. Download the [Black Cauldron demo cartridge](Demo/The-Black-Cauldron-MPE.crt?raw=true)
   and copy it to the TeensyROM+ **SD card**. No game compilation is needed.
3. Launch the CRT from the TeensyROM menu. Follow the
   [demo instructions and controls](Demo/README.md) to start playing.

The demo was compiled from the game hosted on
[Al Lowe's games page](https://allowe.com/downloads/games.html). Its source
credits, cartridge checksum, and verification record are in [`Demo/`](Demo/README.md).
Native game cartridges launch from SD; USB and internal flash do not support
native sessions. The [official restore image](releases/native27/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex?raw=true)
and [recovery instructions](docs/FIRMWARE-GUIDE.md#restore-official-firmware)
remain available.

The [DOSVM package](DOSVM/README.md) is ready to install from the single
`DOSVM/` folder. It includes the matching firmware and CRT, a writable 20 MiB
C: image, and a writable D: drive mapped to SD `/DOSVM/D/`. `MEM`, `XCOPY`,
`MORE` and `ATTRIB` are included. See [DOSVM instructions](dos/README.md) for
controls, the [complete keymap](dos/KEYMAP.md), and [DOS storage](dos/STORAGE.md) for adding games, saving and
upgrading without replacing your working drives.

DOSVM, Boulder, and Might and Magic have been confirmed working on physical hardware.
V1.0.19 retains the black-and-white 80-column DOS console, visible BIOS-style
POST with a short beep, and a blinking cursor. It also retains an optional sharp
320x200 CGA display: press **Ctrl+Commodore+F7** to switch from the default
multicolour renderer. This preserves fine graphics text without changing the
guest CGA mode. Install the paired **R23** CRT while preserving drives. R23
retains R22's physically confirmed cold-start recovery and corrects the
backslash shown in DOS paths and the `C:\>` prompt. When upgrading to V1.0.19,
flash the new firmware and install its R23 `DOSVM.CRT` while preserving both
drives. Run `D:\DOSVMUPD\UPDDOS` if the R20 startup-file update has not already
been applied.
See [display controls](dos/README.md#controls-display-and-sound) for colour
limits and [hardware notes](dos/HARDWARE-TEST.md) for recorded results.

See [firmware release notes](firmware/README.md) for the exact image hashes and
compatibility. Public firmware filenames use `MPE_Firmware-V1.0.19.hex`, with
the final version number increasing for each new release. The GUI's About
panel identifies the installed version. Internal build records for V1.0.19
use the `native27` profile.

V1.0.19/native27 combines the native desktop settings and on-demand apps below
with every M4G2 AGI runtime change from the immutable V1.0.18/native26 release.

Use **F1** for Help, **F2** for BASIC, and **V** to switch between the GUI and
original text menu. **F8 Control Panel > Startup > E** saves the startup menu
style; V remembers the choice too. Home shows a clickable shortcut strip for
**F1 Help, F2 BASIC, F3 SD, F5 USB, F7 MEM, F8 Panel, and V Text**. The same
keys work in file windows without taking space from the fifth icon row.
Check **TEENSY > About MPE Firmware** after an update's reboot to verify the
new desktop is running.

Copy `MPE_Firmware-V1.0.19.hex` to the SD-card root. If the installed GUI reports
**“Firmware selection changed. Choose the file again.”** for that unchanged
file, press **V** and use the original text menu once to install V1.0.19. This
release retains the corrected GUI preflight, which avoids separate SD status
commands during HEX streaming while retaining file identity, size, cancellation
and CRC checks.
After reboot, confirm V1.0.19 in About. The user confirmed the automatic
firmware-update flow worked with V1.0.15.

The current detector scans SD-root names and sizes, offers the highest newer
`MPE_Firmware-Vx.y.z.hex`, and reads the image only after Update is chosen.
Opening or refreshing SD retries discovery. Manual SD selection remains
available; installed and older versions are ignored by automatic discovery,
and updates keep the HEX file. Automatic updating passed the user's physical
V1.0.15 check. See the
[startup update instructions](docs/FIRMWARE-GUIDE.md#future-updates-from-the-sd-card).

## Desktop features

The desktop uses a true 320x200 standard high-resolution VIC-II bitmap, with
one bit per pixel and a foreground/background color pair for each 8x8 cell.
It includes pixel-drawn icons, a six-pixel-spaced font, dotted wallpaper,
outlined menus, and two-line filenames of up to 22 characters with their case,
dots and extensions preserved.

- Complete keyboard operation. Control Panel > Input assigns Mouse or Joystick
  to each port; it permits two joysticks but moves the single supported mouse
  from one port to the other when necessary.
- Folder, disk-image, program, and document icons; shared close, up, and
  scrollbar controls in file windows.
- Parent navigation uses the up control; the desktop hides the synthetic
  `/..` item while preserving the original directory entries and selections.
- Four columns and five rows of icons, with a proportional draggable scrollbar.
  The browser has no bottom filename/status strip; loading, messages, errors,
  and firmware confirmations use centered modal boxes.
- Loading activity fills from left to right and restarts instead of sliding
  across the bar. File-copy progress uses its measured copy/verification state.
- One shared bitmap control library for window frames, clear X close buttons,
  application buttons, modal messages, and firmware confirmations.
- F1 Help, fast icon selection, File > Boot Disk for IEC Drive 8/9, a Music panel,
  and an icon-based Control Panel with an X close button.
- A clickable menu bar, RTC clock, SID play/pause control, and Control Panel.
  A dragged desktop icon keeps a visible ghost under the pointer and shows the
  placement grid; the saved position snaps to that grid.
- Native Control Panel pages provide Appearance choices for Light/Dark mode and
  Dots/Dithered/Blank backgrounds, Input assignments for both control ports,
  and Storage identity/capacity/free-space details for SD, USB, and firmware
  flash.
- Menus reuse the displayed folder's retained background; opening or moving
  through a menu does not recapture filenames. Drawing keeps SID/mouse IRQs active.
- SD mounts are reused across browser, launch, transfer and NFC operations. Empty
  sockets avoid the multi-second mount path; failed cards retry after a bounded
  delay or an explicit refresh.
- Directories use deterministic parent/folder/file ordering and pooled filename
  storage, remaining responsive at the firmware's 4,000-entry limit.
- Drive 8/9 directory browsing and PRG launching, plus SD and USB browsing.
- Snake, Calculator, and the read-only Text Viewer are separate C64 programs
  loaded only when opened. All three are bundled inside the single firmware HEX
  and stream into the shared `$C000` app space when launched; their close button
  or STOP returns to the desktop without a reset.
- The compact original interface remains the boot/recovery path. The desktop,
  settings, and utility programs load into and reuse the same C64 RAM rather
  than remaining resident together.

**Copy** and **Paste** work on individual files in SD and USB folders, including
copies between the two. Paste verifies the copy and refuses an existing
destination filename. **Delete** displays the selected filename and asks for
permanent deletion, with Cancel selected initially. There is no persistent
Trash or recovery store. Folder operations, disk-image contents, and IEC writes
are outside these file operations.

See [File Operations](docs/FILE-OPERATIONS.md) for shortcuts and
[Desktop Usage](docs/CUSTOM-DESKTOP.md) for the complete interface. Open the
[interactive design preview](docs/ui-preview/index.html) through a local HTTP
server to explore the desktop design. The [UI system](docs/UI-SYSTEM.md)
documents the shared controls and their input rules; the
[desktop performance record](Source/C64/MainMenuCRT/UI_PERFORMANCE.md) records
the bounded redraw measurements used by V1.0.19.

## Native MHS Power Engine

The Teensy runs original AGI bytecode, parser handling, motion, collision,
picture and actor rendering, and game state. The C64 presents acknowledged
frames and SID sound and supplies keyboard, joystick, and optional 1351 mouse
input. Native gameplay does not emulate a 6510 or require optional PSRAM.

The MHS Power Engine code remains available in Teensy flash. Its game workspace
is constructed in the shared arena only when a session needs it. Title
playback, an active Power Engine game session, legacy MPE2 compatibility, and
native DOS share one 64 KiB RAM2 arena.
The title hands that arena directly to Power Engine when a game launches.
Title, Power Engine, and MPE2 release it on a clean exit or reset so another
mode can reuse it. DOS seals the arena for reset-only direct execution; leaving
DOS requires a reset before another owner can claim that memory.

Keyboard events and mouse-button transitions use ordered queues at the native
engine boundary. Pointer motion and held joystick direction coalesce to their
latest state, while a full queue leaves the C64 event unacknowledged for exact
retry. This keeps input intact while a large sprite frame is still transferring.

V1.0.19/native27 includes every M4G2 AGI runtime change introduced by the
immutable V1.0.18/native26 release: Fastest has its own scheduler timing,
eligible ego VIEW frames use compact predecoded sidecars with checked raw-VIEW
fallbacks, and unchanged parser/status presentation is not republished. M4G2
also supplies twelve stable manual save slots per game with validated temporary
replacement and backup recovery.

Game resources live in the CRT; the firmware works with compatible game
packages. Small games retain their 1 MiB boot layout, while larger native
packages can use up to 4 MiB with resource banks read by the Teensy. Each M4G2
game has twelve SD save slots in **SAVES**, created automatically. Earlier M4G1
package-CRC saves remain separate. Native CRTs require the matching MPE
firmware; stock firmware and VICE cannot run native gameplay.

The [AGI-64 Compiler](https://meanhamster.com/games/agi-64) remains a separate
project for compiling other supported game sources. Select **MHS Power Engine**
and use its matching firmware kit. See the
[native firmware guide](docs/FIRMWARE-GUIDE.md) for installation, storage,
controls, saves, and recovery.

The combined firmware also retains earlier MPE acceleration services for
compatible older cartridges. Their PowerVM, picture-DMA, and C64 fallback
documentation is collected under [legacy acceleration](docs/Architecture/GENERIC-ACCELERATION.md).
Those interfaces describe a different execution path from the MHS Power Engine
runtime above.

## Source layout

| Path | Contents |
| --- | --- |
| `Source/C64/MainMenuCRT/` | Desktop development sources and focused tests. |
| `Source/Teensy/` | TeensyROM and desktop backend development sources. |
| `engine/` | Native engine, ordered integration patches, selected GUI backend policy, and licensed legacy dependency. |
| `gui/selected-v1.0.19/` | GUI inputs and provenance lock for V1.0.19 / native27. |
| `gui/selected-v1.0.18/` | Preserved GUI inputs used by the immutable V1.0.18 / native26 M4G2 release. |
| `gui/selected-v1.0.17/` | GUI inputs and provenance lock selected for V1.0.17 / native25. |
| `gui/selected-v1.0.14/` | Preserved GUI inputs used by V1.0.14 / native22. |
| `gui/selected-v1.0.12/` | Preserved GUI inputs used by V1.0.12 / native20. |
| `gui/selected-v1.0.11/` | GUI inputs and provenance lock selected for V1.0.11 / native19. |
| `gui/selected-v1.0.10/` | Preserved GUI inputs used by V1.0.10 / native18. |
| `gui/selected-v1.0.9/` | Preserved GUI inputs used by V1.0.9 / native17. |
| `gui/selected-ac4a5d6/` | Preserved GUI inputs used by native08. |
| `gui/selected-e305/` | Preserved GUI inputs used by native05 through native07. |
| `scripts/` | Combined firmware builder, GUI assembly, and validation tools. |
| `tests/` | Native engine, session, cartridge, and firmware checks. |
| `firmware/` | Only the current combined firmware image and its README. |
| `docs/firmware/` | Current download checksums and source lock. |
| `releases/` | Immutable firmware kits, restore images, and source manifests. |
| `Demo/` | Ready-to-use Black Cauldron CRT, instructions, credits, and checksums. |
| `DOSVM/` | DOSVM distribution: firmware, cartridge, fresh disk and SD-folder files. |
| `dos/` | DOSVM documentation, sources, distribution inputs and automated checks. |

The combined builder consumes the locked GUI snapshot in `gui/` and the
integration sources in `engine/`. A change in the desktop development tree
must be reviewed and incorporated into that selected snapshot before it
becomes part of a new native release; backend changes also require a matching
backend patch and policy. Changes under `Source/` do not become native27 build
inputs until the new snapshot and release configuration are locked.

## Build the combined firmware on Windows

Install Git, Node.js 20.11 or later, PowerShell, and ACME 0.97. First follow the
[locked release-source instructions](docs/FIRMWARE-GUIDE.md#reproduce-the-release-source)
to check out the exact `engineCommit` in `docs/firmware/source.lock.json`.
This reproduces the 47-patch combined MHS Power Engine release, including the
separately recorded Power Engine game-runtime sources, native DOS sources, and
one shared native runtime source. Later development on `main` can use a
different builder. From that worktree's root, run:

```powershell
.\scripts\build-firmware.ps1 -CustomGuiAcmePath C:\Tools\ACME\acme.exe
```

The builder obtains the pinned upstream source and Arduino CLI 1.4.1, Teensy
core 1.61.0, and CRC32 2.0.0 when needed. It verifies the patch chain and GUI
inputs, assembles and checks the selected C64 menu, runs conformance checks,
builds both firmware halves, and checks memory reserves. It does not flash
hardware.

Output defaults to `build/native27/`, with disposable source in `source/`,
firmware in `firmware/`, and provenance in `manifests/`. The toolchain cache
defaults to `build/toolchain/`. Use `-ToolchainRoot` and `-OutputRoot` to select
other locations; ACME can also be on `PATH`. Use `-SourcePath` only for a
disposable checkout at the pinned upstream commit.

See [Build Provenance](docs/BUILD-PROVENANCE.md) for source pins and release
records. The lower-level `Source/Teensy/tools/Build-DualBoot.ps1` workflow is
for the `Source/` development tree; use the root builder above to reproduce
the combined native MPE release.

## Development checks and hardware status

Focused desktop checks run from the repository root:

```powershell
node Source/Teensy/MinimalBoot/tests/agi-picture-conformance.mjs
node --test Source/C64/MainMenuCRT/tests/*.test.js Source/C64/MainMenuCRT/tests/*.test.mjs
node scripts/generate-desktop-bitmap-assets.mjs --check
```

For desktop development, rebuild the C64 menu with
`scripts/build-c64-menu.ps1 -AcmePath C:\Tools\ACME\acme.exe` before building
the `Source/` firmware tree. That script accepts `-PythonPath` or uses Python
from `PATH`. See [native test instructions](tests/README.md) for the engine
and cartridge checks; the full game test catalog requires separate fixtures.

Each release records its build and host checks. Physical C64/128,
SD/USB file-operation, and mouse acceptance remain separate. The Black
Cauldron demo has passed native startup, input, rendering, and loader checks;
a complete playthrough and physical gameplay have not been verified for this
download.

## Credits

The custom GUI, MHS Power Engine and DOSVM integration are developed by **John Swiderski** of
**[Mean Hamster Software](https://meanhamster.com)**. The desktop's About panel
shows these credits, the installed MPE firmware version, and
**www.MeanHamster.Com**. It closes through the standard X control used by the
other desktop windows.

Based on [SensoriumEmbedded/TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM)
at commit `3436b8fbd7c642ef9eabc691d3d09da08a6a6690`, with upstream
notices retained. See [LICENSE.md](LICENSE.md),
[the TeensyROM license](docs/TEENSYROM-LICENSE.md), and the dependency licenses
in `engine/vendor/`. The included game's original ownership and attribution
are documented in [Demo credits](Demo/README.md#source-and-credits).
