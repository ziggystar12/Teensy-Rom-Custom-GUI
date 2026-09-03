# TeensyROM Desk

TeensyROM Desk is a GEOS-inspired replacement for the C64-side TeensyROM file
list. It retains the existing firmware-update, SID, picture, text, NFC, and
Teensy launch services, with native bitmap presentation, local input routing,
disk-image directory fixes, individual-file operations on SD/USB, and a
separate IEC program-launch path.

This is an original interface inspired by the compact monochrome desktop
language of GEOS.  It does not copy GEOS code, fonts, icons, or other assets.

## Desktop contract

- The expanded browser uses the VIC-II's standard 320x200 high-resolution
  bitmap mode. Each bitmap bit is one independently addressable pixel; the
  corresponding screen byte selects the two colors for that 8x8 cell.
- VIC-II multicolor mode remains off. Pixels are not doubled horizontally and
  are not encoded as two-bit color pairs. Normal cells use black and white;
  accent, selection, clock, and status cells each choose their own two-color
  pair through the same standard high-resolution screen byte.
- The desktop shows a scrolling four-column by four-row icon viewport with a
  proportional scrollbar. The classic recovery list retains its 19-item pages.
- The desktop omits the synthetic `/.. <Up Dir>` entry from icons, status
  text, and item counts. Use the window Up control or Up-arrow key instead.
  The original entry remains available in the classic text list.
- Directories use a folder icon, D64/D71/D81 images use a floppy icon,
  executable files use a program-window icon, and other files use a folded
  document icon. Each icon is original 24x16 monochrome pixel artwork.
- Each icon has a two-line label preserving case, dots and extensions, such as
  `Text.txt`. Long labels retain a short extension with an ellipsis. Delete and
  firmware-update confirmations retain the full name.
- Existing IO1 register meanings and Teensy-backed SD/USB file services are
  retained; a volatile menu-view register selects desktop filtering while
  preserving the backend's raw file indices. The separate C64 KERNAL IEC launch path adds the handoff command
  described below; its disk loading and errors are handled on the C64.
- The icon desktop is the default.  Uppercase `V` toggles the original list
  view as a recovery path. F1 opens Help; F7 selects Teensy memory.

## Input

Keyboard and joystick always remain available:

- Cursor keys or joystick 2 move the selection through the grid.
- Return or joystick fire opens the selected folder/file.
- Up-arrow moves to the parent folder.
- HOME returns directly to the desktop; STOP closes a panel or returns from
  the folder browser. Folder views have drawn close and up-arrow gadgets.
- F1 opens Help. F3, F5, and F7 select SD, USB, and Teensy memory.
- F2 exits to BASIC; F4 controls SID playback; F6 opens Music; F8 opens Control Panel.
- Shift+RUN/STOP boots the selected IEC disk through File > Boot Disk. Plain
  RUN/STOP keeps its Back/Cancel behavior.
- Shift+C copies the selected SD/USB file to the clipboard; Shift+P pastes
  it into the current folder; Shift+D prepares permanent deletion. The same
  actions are in Edit > Copy/Paste and File > Delete.
- Existing letter-search, autolaunch, NFC, REU, KERNAL, disk-mount, and hot-key
  commands remain available.

The physical Shift+cursor combinations retain their normal Up/Left codes.
Vertical desktop movement skips empty snap rows while keeping the column.
Joystick 2 is sampled only while both CIA ports are isolated from keyboard
scan outputs. Mouse-button-held samples suppress ambiguous joystick readings;
ordinary mouse movement does not block keyboard or joystick input. Both mouse
button edges require two agreeing IRQ samples to reject one-sample glitches.

The existing input arrangement is intentionally retained: a Commodore 1351 in
control port 1 and the established joystick in control port 2. Motion drives a VIC-II
sprite pointer.  A click selects the icon under the pointer; clicking the
selected icon again opens it.  Keyboard and joystick input do not depend on
mouse detection.  Click targets also expose parent-folder navigation,
scrolling, Teensy/SD/USB sources, Help, Settings, the view toggle, and
the play/pause icon immediately left of the clock. The icon shows pause bars
while the background SID is playing and a play triangle while it is paused;
`F4` remains the keyboard equivalent.

Native icon clicks are limited to the displayed 24x16 artwork and its actual
filename/label, including a second text line when present. Blank space does
not select an item or redraw. Home drag targets use the same 60x54 pixel
spacing as the rendered icons, including icons moved to another slot.

Leaving the mouse/SID IRQ restores the KERNAL keyboard data directions before
reenabling interrupts. A held mouse button can no longer leave scanning
permanently masked at a firmware y/n prompt; the explicit confirmation gate
is unchanged.

## Display implementation

The expanded `DesktopShell.prg` uses a full 8,000-byte VIC-II bitmap plus its
1,000-byte screen matrix. Bitmap bits select between the background and
foreground nibbles of the corresponding screen byte, giving exactly two colors
per 8x8 cell while retaining the full 320-pixel horizontal resolution. Text,
lines, menus, and the original monochrome icon artwork are rasterized into that
bitmap; this is standard high-resolution bitmap mode, not 160x200 multicolor
bitmap mode.

Desktop windows, icons and labels are drawn in pixel coordinates. The shared
[control library](UI-SYSTEM.md) supplies their frames, close buttons, buttons,
scrollbars and dialog input. A protected layout buffer at `$4000` also holds
pending color cells; new colors are published after the corresponding bitmap
pixels. Partial control updates preserve pixels outside their exact bounds.
Selection is drawn in black-and-white bitmap pixels to avoid leaking a label's
color into an adjacent icon through a shared 8x8 cell.

Directory waits, errors, file operations and firmware confirmations use the same
bitmap dialog family. Activity bars show work without inventing a percentage;
copy progress uses the backend's actual byte count. The firmware prompt keeps
mouse sampling active, defaults to Cancel, and begins the existing updater
handshake only after explicit confirmation. The compact recovery menu remains
available in character mode.

The resident 8 KiB cartridge remains a compact bootstrap and recovery menu. It
loads the expanded `DesktopShell.prg` from Teensy flash through the existing PRG
loader, so the richer interface does not consume the Teensy's timing-critical
resident RAM margin. The compact menu and the desktop's classic list view keep
the established character-mode renderer, providing a hardware recovery path if
the expanded shell cannot be loaded or its bitmap view is disabled.

Picture viewers may temporarily reuse the VIC-II display memory; returning to
the expanded shell redraws its bitmap. Background SID files whose load range
overlaps `$2000-$47ff` are rejected while the desktop and SID coexist, in
addition to the separate menu-code overlap check. The mouse pointer is hidden
while a viewer, classic-list mode, or external program owns the screen. Native
dialogs restore the pointer after their bounded redraw.

## File operations

Copy, Paste, and Delete work on individual regular files in ordinary SD and
USB folders, including whole `.D64`/`.D71`/`.D81` image files and `.hex` firmware
files. Copying a firmware file does not start the updater. Directories, files
inside a disk image, built-in Teensy files, and Drive 8/9 IEC writes are outside
this feature. Filenames must use printable ASCII (bytes 32 through 126) so
the full target can be displayed. Rename, Cut/Move, and New Folder are not
implemented.

Copy records the selected source path and filename in Teensy RAM. Navigate to
a destination SD/USB folder and choose Paste; cross-device SD-to-USB and
USB-to-SD copies use the same path. The source bytes are read when Paste starts.
The clipboard is not saved to disk and is lost when the Teensy firmware
restarts. Deleting its source clears it.

Paste refuses any existing destination name, including a same-folder copy.
It copies one bounded chunk per firmware poll into a temporary file, then reads
that file back and checks its size and CRC before giving it the requested
name. Progress is displayed; STOP, Escape, or the Cancel button requests
cancellation. Normal cancellation/errors close the files and remove the
partial copy. If power or storage loss prevents cleanup, a `.tr-copy-*.tmp`
file may remain and can be deleted after reconnecting. There is no persistent
trash or recovery system.

Delete first captures the selected full path and file metadata. Its dialog
shows the filename and asks for permanent deletion, with Cancel selected.
Return/fire therefore cancels until the arrows select Delete; Y or a click on
Delete confirms directly. STOP, Escape, N, or Cancel dismisses the request.
The backend rechecks the prepared file before deleting it and does not follow
a later cursor change. Success refreshes the directory; storage/read/write
errors remain visible in the dialog. Navigation and launches are blocked while
a copy or delete confirmation is active.

Install the complete [File Operations firmware](FILE-OPERATIONS.md)
so the C64 UI and Teensy commands match. The combined firmware
includes the native desktop and MHS AGI engine; its exact source
revisions are recorded in the release manifest. See the
[firmware release notes](../firmware/README.md) and
[Black Cauldron demo](../Demo/README.md). The native07/e305 kit remains a
historical rollback.

## Implemented desktop shell

The interface represented by `mockup/index.html` uses the 320x200 standard
high-resolution bitmap renderer and keeps the established input arrangement.
The home surface uses the actual mockup's 24x16 artwork, dotted wallpaper, and
5x7 font with six-pixel spacing. Icons and centered labels are drawn at pixel
positions, not assembled from character cells. The black header, outlined
dropdowns, Control Panel, browser frame, and clock are bitmap-native too.
The header is eight pixels high to preserve the browser's existing title row.
Browser filenames use two lines of ten characters, fitting complete standard
16-character C64 filenames. Longer SD/USB names are shortened under the icon;
operations that need confirmation display the full name in their dialog.
The current source adds a clickable
`Teensy / File / Edit / View / Disk` header, an RTC-backed clock with seconds and a dynamic
SID play/pause control, top-level icons
for Teensy memory, SD, USB, Drive 8, Drive 9, Control Panel, and two folders,
plus snap-grid desktop icon movement persisted by the Teensy. Mouse, joystick 2,
and keyboard share the same activation paths.

Clicking the same open menu header closes it; another header switches menus.
Clicking outside the dropdown dismisses it without activating whatever is
underneath, including the clock-adjacent SID button. Click again to use that
control after the menu has closed.

The menu bar starts with the clickable Teensy menu, without a separate brand
label. Browser navigation lives in the framed title/path rows: a drawn close
gadget returns to the desktop, an up arrow opens the parent, and distinct
previous/next arrows turn pages. The page count itself is not a button.
The bottom has one clickable F-key strip. All five icon rows fit above it.
Loading progress, backend messages, and errors appear in a centered modal
dialog instead of writing over the lower icon rows. The progress bar indicates
activity; it does not claim a percentage. Notices remain visible when a command
needs an explanation.

The clock displays HH:MM:SS, with A/P in 12-hour mode. Each refresh uses one
coherent CIA time snapshot and releases its read latch before drawing; time,
format, and SID-state changes refresh the same isolated header region.

This is intentionally a single-surface desktop with one active folder, menu,
or modal panel. Arbitrary overlapping windows and z-order backing stores are
outside the first implementation. Folder contents remain automatically
arranged; only top-level desktop icons are freely moved and persisted.

Drive 8/9 icons read the actual device directory through the C64 KERNAL IEC
channel API. Each page shows up to 25 entries; page controls and cursor/joystick
navigation reach subsequent entries. Full filenames fit beneath their icons.
HOME, STOP, and the close gadget return directly to the desktop. R refreshes.
SD2IEC DIR entries and `.D64`/`.D71`/`.D81` files can be entered using its CD
command; Parent sends the standard CD-left-arrow command. Real floppies display
their flat directory and reject unsupported CD commands normally.

PRG entries can now be launched by Return, joystick fire, or a second mouse
click. Enter an SD2IEC image first, then select its boot/program file. The
launcher uses the selected device (8 or 9) and KERNAL LOAD with secondary
address 1. Standard $0801 BASIC programs and SYS boot stubs run automatically;
other PRGs load at their own address and return to BASIC for an explicit SYS.
Files loading below $0800 are rejected because they overlap loader/workspace.
Launching replaces the desktop; use the cartridge's menu/reset control to
return. This IEC path does not copy, save, or delete drive files.

The launch path preflights the filename/address while errors can still return
to the browser. It then restores KERNAL/BASIC state and relocates the loader,
filename, device, and launch metadata into the 192-byte tape buffer. Programs
can therefore overwrite the desktop without destroying their own loader.
`rCtlRunningIEC` (56/$38) uses the existing handoff to the configured next IO
handler, without consulting a stale Teensy file selection. Install the C64
menu and full firmware together; old firmware does not implement this command.

The SID is paused while transferring directory/command data and stopped for
launch. Missing drives and preflight errors are shown in the browser; errors
during the subsequent LOAD return to BASIC. A physically wedged IEC bus can
still stall a stock KERNAL serial handshake. No hard hardware timeout is claimed.

SD/USB disk-image browsing and IEC browsing both support 25 entries per page.
Use the page arrows or move vertically past the icon grid to page through an
SD/USB image. Empty or scratched D64/D71/D81 directory slots no longer hide
later entries in the same directory sector. Opening an image through SD/USB
lists and extracts its files; it is not equivalent to mounting a drive, and
does not by itself guarantee that a GEOS boot disk can run.

The SD2IEC Snoop project and original Commodore sources were protocol
references, not copied implementation code:
[SD2IEC Snoop](https://github.com/exrom/sd2iec-snoop),
[Commodore sources](https://github.com/mist64/cbmsrc).

The Control Panel presents nine category icons with matching artwork/label hit
areas. Click once to select, then again to open, or use arrows and Return/fire.
Click X, or press STOP, HOME, Escape, or F8 to close it. Background file actions
are blocked while the panel is open. Repeated selection does not redraw; changed
selection updates only the old/new labels. The eight original categories open
the existing keyboard-controlled Settings program; Music opens the native panel.

Press F6 or open Control Panel > Music to manage background music. Choose Browse,
open a `.sid` file from Teensy memory, SD, or USB, then choose Use Default to save
that loaded SID as the background track. Play/Pause controls playback now;
Autoplay toggles playback at startup. The current track and startup state appear
inside the Music panel. Advanced retains subsong, speed, voice mute, and border
controls in the detailed SID screen; Space returns to Music.

File > Boot Disk (Shift+RUN/STOP) uses `LOAD "*",8,1` or device 9, then starts the
loaded program. On the desktop select Drive 8 or Drive 9; in an IEC window a
selected folder or D64/D71/D81 image is entered first. An ordinary selected file
boots the current disk instead of loading that file by name. This requires a
real IEC drive or compatible device such as SD2IEC. Teensy SD/USB image browsing
extracts individual files and does not emulate an IEC drive. Some GEOS disks need
a particular boot file or compatible drive setup, so wildcard boot is not a
promise that every GEOS image runs.

 `File > Firmware Update` opens the normal SD browser (F5 switches to
USB); selecting a `.hex` still reaches the established lowercase `y`/`n`
confirmation and warning before the Teensy updater is started. The desktop menu
does not contain a direct-flash shortcut.

The File Operations build enables Edit > Copy/Paste and File > Delete for
SD/USB files. The eight-icon home surface has no Trash icon. Permanent deletion
has an explicit Cancel-first confirmation; see the file-operation behavior
and storage limits above.

## Resident desktop demo apps

Click **TEENSY at the top-left of the menu bar**, then choose **Snake**,
**Calculator**, or **Text Viewer**. With the keyboard, press **HOME**, then
**RUN/STOP** to open that menu. Press Down four times for Snake, five for
Calculator, or six for Text Viewer, then **RETURN**. The Games and Utilities
desktop icons open folders of built-in files; use the top menu for these apps.

These are native black-and-white bitmap app windows. The desktop remains
resident: STOP, HOME, Escape, or the drawn close button returns
to the previous surface without a reboot. Mouse controls and keyboard controls
use the existing input sampler; Snake also accepts joystick directions/fire.

- Snake: cursor keys or WASD steer, P/Space pauses, R restarts, and drawn mouse
  buttons provide the same actions. Food grows the snake; walls and its body
  end the game. The demo board is 16x12 cells with a 64-cell winning length.
- Calculator: signed 16-bit integer arithmetic (`-32768` through `32767`) with
  `+`, `-`, `*`, and `/`; division truncates toward zero. Digits/operators can be
  clicked or typed, Return/`=` evaluates, and C clears. Overflow and division by
  zero show ERROR. Floating-point/decimal arithmetic is outside this demo.
- Text Viewer is read-only; there is no Notepad editor or text-saving feature.
  OPEN (or O) returns to the SD browser to select a TXT/NFO/MD/SEQ
  file. Files opened from Teensy memory, SD, or USB use the bitmap viewer while
  icon view is active; the classic view retains its legacy text viewer. Lines
  wrap at 48 characters; 17 lines fit per page. PREV/NEXT or cursor keys page
  backward/forward, rereading the read-only stream for backward navigation.
  Paging is bounded to 255 pages. PETSCII screen/color controls are ignored.
  Raw IEC drive SEQ files are not connected to this viewer yet.

The apps are assembled separately at `$c000-$cfff` and appended to the desktop
PRG. Both the production and VICE loaders copy this extension before relocating
the main payload, preventing overlapping source data from being destroyed.
SID loading protects `$a000-$cfff` (composition plus apps). App repaint keeps
the frame and clears only its bitmap interior instead of rebuilding the desktop.
Ordinary Snake moves update only the old tail and new head; food/state changes
redraw its interior. VICE measured ordinary moves at 158,707 CPU cycles (about
0.16 seconds PAL), versus 2,582,460 cycles for the original full-frame redraw.

All 142 focused checks pass, including actual assembled app arithmetic/input,
text paging, exact window pixels and click targets, color publication order,
incremental/full-frame equivalence, and byte-exact loader/memory boundaries.
Actual VICE input checks additionally verified `12+3=15`, Snake pause/restart/
movement, and both STOP and mouse close returning to the desktop.

## Validation boundary

The C64 menu assembly must be rebuilt before the dual TeensyROM+ firmware, or
the firmware will silently embed the previous `TeensyROMC64.h` menu image.
Assembly size and 8 KiB cartridge bounds are build gates.  A VICE mock can
exercise rendering and local navigation, but VICE does not emulate the
TeensyROM IO1 file backend.  SD/USB browsing, folder traversal, launching,
firmware-update selection, mouse behavior, and return-to-menu all require a
real C64/128 with TeensyROM+ acceptance pass. IEC 8/9 directory reads can also
be tested against actual VICE-emulated drives; SD2IEC CD navigation needs
physical SD2IEC acceptance.

Focused source/input/asset/app tests and AGI firmware conformance are
maintained with the source. File-operation host tests exercise storage faults
and the assembled C64 confirmation/input paths; this is not physical storage
acceptance. Earlier validation also passed 47 assembled IEC parser tests. VICE read distinct drive-8 and
drive-9 D64 fixtures; the 24-file disk pages as 19 then 5 entries. Missing drive
handling and direct Desktop return were checked, and fixture hashes remained
unchanged. Completed redraws report standard bitmap mode with multicolor off.
These checks do not replace physical C64/SD2IEC testing.

The counts below record completed validation runs. Their temporary emulator
logs, generated disk fixtures, and one-off probes were removed during cleanup;
the maintained source/model regression tests remain under `tests/`.

The earlier nine-icon native VICE preview matched all 3,456 desktop-icon pixels and
2,135 label-glyph pixels against the mock-derived assets. Actual menu open/close
restores every non-clock bitmap byte; 119 production/preview routing checks pass.
The native Control Panel and two-line IEC filename view were captured as well.
Fourteen assembled clock cases verify 6,944 exact glyph/media pixels, including
seconds, minute changes, 12/24-hour switching, midnight/noon, and SID toggles.
Clock refreshes leave the body, color matrix, pointer registers, and bank intact.
An additional 212 assembled mouse cases cover native icon/label boundaries,
blank gaps, moved icons, drag origins, and empty-row cursor wrapping. Empty clicks leave all bitmap bytes
unchanged and make zero redraw calls. Held/released IRQ handoffs restore the
keyboard directions; explicit Y/N inputs exercise the real confirmation gate
with Teensy metadata/output and the flash side effect mocked, never flashed.

A separate input probe executes the assembled sampler and stock KERNAL scanner
against a CIA keyboard-matrix model: both physical Shift keys produce Up/Left,
18 home/folder/IEC key routes work, all five joystick controls remain available,
and moving-mouse click/noise/hold/release sequences are debounced. This does not
replace physical input acceptance.

Twenty real-drive VICE cases cover D64/1541, D71/1571, and D81/1581 on devices
8 and 9: short programs, 16-character filenames, and large programs through
$9fef load and RUN, overwriting the desktop and preserving $ba's device number.
Missing-file preflight errors remain in the browser. Only the unavailable
Teensy handoff is mocked; KERNAL OPEN/CHRIN/LOAD/RUN execute unchanged and
attached disk hashes remain unchanged.
Three final-build checks repeat long D64/drive-8 and D81/drive-9 launches and
verify that a native $c000 file loads without executing and returns to BASIC.

The bitmap compositor prepares frames in RAM under BASIC (`$a000-$bfff`) and
copies only changed bytes to the visible `$2000` bitmap. SID IRQ playback
preserves the interrupted memory mapping. The desktop payload starts at `$4800`
and stays below `$a000`; both PRG loaders relocate backwards so their enlarged
source image can overlap its destination safely. SID loading rejects the
bitmap, font/layout, code, and new composition-buffer ranges. The compact
recovery cartridge still starts at `$6000`.

Reproduce/check the mock-derived assets with
`node scripts/generate-desktop-bitmap-assets.mjs --check`.

## VICE UI preview

Run `Source/C64/MainMenuCRT/preview-desktop.ps1` to build the hardware-free UI
preview. `-Capture` saves its home screenshot; `-Capture -Menu` captures a menu.
The resulting `build/vice-preview/DesktopPreview.prg` can be autostarted in VICE.
Keys 1-5 open menus, 6-8 launch Snake/Calculator/Text Viewer, F8 shows the
Control Panel, and HOME/STOP returns home. `-App 0`, `-App 1`, or `-App 2`
starts directly in a demo app; `-Capture` can capture those screens too.
App close and calculator/Snake controls run normally; the preview text OPEN
button is disabled because there is no Teensy file backend.
Teensy-backed actions are disabled in this preview. `-IECDevice 8` or `9`, with
`-Drive8Image`/`-Drive9Image`, exercises the real IEC reader on an attached disk
image. Images are attached read-only. Preview files do not replace firmware.
