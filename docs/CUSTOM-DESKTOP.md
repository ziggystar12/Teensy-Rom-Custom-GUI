# TeensyROM Desk

TeensyROM Desk is a GEOS-inspired replacement for the C64-side TeensyROM file
list.  It keeps the existing Teensy directory, disk-image, loader, firmware
update, SID, picture, text, NFC, and launch services unchanged.  Only the C64
menu presentation and local input routing change.

This is an original interface inspired by the compact monochrome desktop
language of GEOS.  It does not copy GEOS code, fonts, icons, or other assets.

## Desktop contract

- The browser uses the VIC-II's native 320x200 one-bit high-resolution
  character display: 40x25 cells, with every glyph dot remaining an
  individually designed C64 pixel.
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
previous/next page, Teensy/SD/USB sources, Help, Settings, and the view toggle.

## Display implementation

The desktop copies the C64 lower-case ROM font into RAM at `$3800-$3fff`, then
replaces 24 private glyphs with original folder, floppy, document, and program
art. Six adjacent 8x8 glyphs form each 24x16 icon. This gives a sharp 320x200
black-on-white display without the 8,000-byte cost of a full bitmap, while
preserving KERNAL text output and the 8 KiB menu-cartridge limit.

Picture viewers may temporarily reuse that RAM and the desktop reinstalls the
font when it returns. Background SID files that overlap only `$3800-$3fff` are
rejected while the menu is resident; the existing, separate menu-code overlap
check remains unchanged. The mouse pointer is hidden while a viewer, dialog,
classic-list mode, or external program owns the screen.

## Scope of the first release

The first release covers visual browsing and the file actions TeensyROM
already exposes: enter folders and disk images, open/view supported files,
launch programs and cartridges, mount disk images, choose firmware/REU/KERNAL
files, and return to the browser.

Rename, copy, move, delete, and new-folder commands are a separate second
phase.  They require new Teensy control commands, progress/error responses,
write-protection handling, and confirmation dialogs; they should not be
introduced implicitly as part of the visual rewrite.

## Approved desktop-shell direction

The next UI milestone is represented by `mockup/index.html` and keeps the same
320x200 monochrome rendering and input arrangement. It adds a clickable
`Desk / File / Edit / View / Disk` header, an RTC-backed clock, top-level icons
for Teensy memory, SD, USB, Drive 8, Drive 9, Control Panel, folders, and Trash,
plus snap-grid desktop icon movement. Mouse, joystick 2, and keyboard paths must
remain functionally equivalent.

This is intentionally a single-surface desktop with one active folder, menu,
or modal panel. Arbitrary overlapping windows and z-order backing stores are
outside the first implementation. Folder contents remain automatically
arranged; only top-level desktop icons are freely moved and persisted.

Drive 8/9 icons initially represent mount targets and status. The existing
Meatloaf path supports a hard-coded Drive 8 mount; a slot parameter and state
query are required for Drive 9. Native dual-IEC-drive emulation is a separate
feature from the desktop UI.

## Validation boundary

The C64 menu assembly must be rebuilt before the dual TeensyROM+ firmware, or
the firmware will silently embed the previous `TeensyROMC64.h` menu image.
Assembly size and 8 KiB cartridge bounds are build gates.  A VICE mock can
exercise rendering and local navigation, but VICE does not emulate the
TeensyROM IO1 file backend.  SD/USB browsing, folder traversal, launching,
firmware-update selection, mouse behavior, and return-to-menu all require a
real C64/128 with TeensyROM+ acceptance pass.
