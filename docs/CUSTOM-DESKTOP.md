# TeensyROM Desk

TeensyROM Desk is a GEOS-inspired replacement for the C64-side TeensyROM file
list.  It keeps the existing Teensy directory, disk-image, loader, firmware
update, SID, picture, text, NFC, and launch services unchanged.  Only the C64
menu presentation and local input routing change.

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
- One firmware page remains 19 items.  The desktop arranges those items in a
  five-column by four-row icon grid, leaving the twentieth cell unused.
- Directories use a folder icon, D64/D71/D81 images use a floppy icon,
  executable files use a program-window icon, and other files use a folded
  document icon. Each icon is original 24x16 monochrome pixel artwork.
- Each icon has a short label.  The complete selected name is shown in the
  status area, so long names remain inspectable before launch or update.
- The existing IO1 register interface remains unchanged.  Directory loading,
  sorting, nested folder traversal, virtual disk-image traversal, execution,
  and error reporting continue to be owned by the Teensy firmware.
- The icon desktop is the default.  Uppercase `V` toggles the original list
  view as a recovery path.  Existing function-key and action shortcuts retain
  their meanings.

## Input

Keyboard and joystick always remain available:

- Cursor keys or joystick 2 move the selection through the grid.
- Return or joystick fire opens the selected folder/file.
- Up-arrow moves to the parent folder.
- HOME returns directly to the desktop; STOP closes a panel or returns from
  the folder browser. Folder views also have clickable [X], Desktop, Parent,
  and Open controls.
- F1, F3, and F5 select Teensy memory, SD, and USB.
- F2 exits to BASIC; F4 controls SID playback; F6 shows SID information; F7
  opens Help; F8 opens Settings.
- Existing letter-search, autolaunch, NFC, REU, KERNAL, disk-mount, and hot-key
  commands remain available.

The existing input arrangement is intentionally retained: a Commodore 1351 in
control port 1 and the established joystick in control port 2. Motion drives a VIC-II
sprite pointer.  A click selects the icon under the pointer; clicking the
selected icon again opens it.  Keyboard and joystick input do not depend on
mouse detection.  Click targets also expose parent-folder navigation,
previous/next page, Teensy/SD/USB sources, Help, Settings, the view toggle, and
the play/pause icon immediately left of the clock. The icon shows pause bars
while the background SID is playing and a play triangle while it is paused;
`F4` remains the keyboard equivalent.

## Display implementation

The expanded `DesktopShell.prg` uses a full 8,000-byte VIC-II bitmap plus its
1,000-byte screen matrix. Bitmap bits select between the background and
foreground nibbles of the corresponding screen byte, giving exactly two colors
per 8x8 cell while retaining the full 320-pixel horizontal resolution. Text,
lines, menus, and the original monochrome icon artwork are rasterized into that
bitmap; this is standard high-resolution bitmap mode, not 160x200 multicolor
bitmap mode.

Layout is composed in a protected 1 KiB, KERNAL-aligned canvas at `$4000`.
The visible bitmap is retained during refreshes; unchanged glyph bytes are
not rewritten. The 128-glyph font is stored at `$4400`, outside bitmap memory, with
reverse video supplied by color pairs. Directory waits use a bitmap status
line, while legacy launch and firmware-confirmation pages retain text mode.
The layout still uses an 8x8 text grid: bitmap mode alone does not make it a
free-form windowing system.

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
while a viewer, dialog, classic-list mode, or external program owns the screen.

## Scope of the first release

The first release covers visual browsing and the file actions TeensyROM
already exposes: enter folders and disk images, open/view supported files,
launch programs and cartridges, mount disk images, choose firmware/REU/KERNAL
files, and return to the browser.

Rename, copy, move, delete, and new-folder commands are a separate second
phase.  They require new Teensy control commands, progress/error responses,
write-protection handling, and confirmation dialogs; they should not be
introduced implicitly as part of the visual rewrite.

## Implemented desktop shell

The interface represented by `mockup/index.html` uses the 320x200 standard
high-resolution bitmap renderer and keeps the established input arrangement.
The current source adds a clickable
`Desk / File / Edit / View / Disk` header, an RTC-backed clock with a dynamic
SID play/pause control, top-level icons
for Teensy memory, SD, USB, Drive 8, Drive 9, Control Panel, folders, and Trash,
plus snap-grid desktop icon movement persisted by the Teensy. Mouse, joystick 2,
and keyboard share the same activation paths.

This is intentionally a single-surface desktop with one active folder, menu,
or modal panel. Arbitrary overlapping windows and z-order backing stores are
outside the first implementation. Folder contents remain automatically
arranged; only top-level desktop icons are freely moved and persisted.

Drive 8/9 icons read the actual device directory through the C64 KERNAL IEC
channel API. Each page shows up to 19 entries; page controls and cursor/joystick
navigation reach subsequent entries. Full selected names appear in the status
strip. HOME, STOP, [X], and Desktop return directly to the desktop. R refreshes.
SD2IEC DIR entries and `.D64`/`.D71`/`.D81` files can be entered using its CD
command; Parent sends the standard CD-left-arrow command. Real floppies display
their flat directory and reject unsupported CD commands normally.

This IEC view is read-only: regular files are listed, not launched, copied or
deleted. The SID is paused only while transferring directory/command data.
Missing drives and read/DOS errors are shown in the browser. Parser work is
bounded, but a physically wedged IEC bus can still stall a stock KERNAL serial
handshake. No hard hardware timeout is claimed.

The SD2IEC Snoop project and original Commodore sources were protocol
references, not copied implementation code:
[SD2IEC Snoop](https://github.com/exrom/sd2iec-snoop),
[Commodore sources](https://github.com/mist64/cbmsrc).

The Control Panel routes its categories into the existing nine-page Settings
program. `File > Firmware Update` opens the normal SD browser (F5 switches to
USB); selecting a `.hex` still reaches the established lowercase `y`/`n`
confirmation and warning before the Teensy updater is started. The desktop menu
does not contain a direct-flash shortcut.

Copy and Paste are present to establish the intended menu arrangement, but they
currently show an unavailable notice. Trash is likewise disabled. Implementing
those mutations still requires bounded Teensy commands, write-protection and
progress handling, error reporting, and confirmation dialogs.

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

Current checks: 49 focused source/input tests, 47 assembled IEC parser tests,
and the AGI firmware conformance suite pass. VICE reads distinct drive-8 and
drive-9 D64 fixtures; the 24-file disk pages as 19 then 5 entries. Missing drive
handling and direct Desktop return were checked, and fixture hashes remained
unchanged. Completed redraws report standard bitmap mode with multicolor off.
These checks do not replace physical C64/SD2IEC testing.

## VICE UI preview

Run `Source/C64/MainMenuCRT/preview-desktop.ps1` to build the hardware-free UI
preview. `-Capture` saves its home screenshot; `-Capture -Menu` captures a menu.
The resulting `build/vice-preview/DesktopPreview.prg` can be autostarted in VICE.
Keys 1-5 open menus, F8 shows the Control Panel, and HOME/STOP returns home.
Teensy-backed actions are disabled in this preview. `-IECDevice 8` or `9`, with
`-Drive8Image`/`-Drive9Image`, exercises the real IEC reader on an attached disk
image. Images are attached read-only. Preview files do not replace firmware.
