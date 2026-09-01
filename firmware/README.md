# TeensyROM Desk preview firmware

`MHS-PowerEngine-TRPlus-GEOS-DESKTOP-PREVIEW_full.hex` is a separately named
TeensyROM+ Fab0.4 preview. It contains the existing MHS Power Engine firmware
plus the GEOS-inspired C64 menu described in `../docs/CUSTOM-DESKTOP.md`.

- Display: native 320x200 black-on-white high-resolution character mode
- Icons: original 24x16 pixel folder, floppy, document, and program artwork
- 1351 mouse: control port 1 (unchanged)
- Joystick: control port 2 (unchanged)
- Keyboard: always available
- Uppercase `V`: switch between icon and classic-list views
- First icon click: select; second click on the same icon: open
- Built only; it has not been flashed or accepted on physical hardware

The existing `MHS-PowerEngine-TRPlus-v1_full.hex` file was not replaced or
modified. Background SID files overlapping the desktop font RAM at
`$3800-$3fff` are rejected while the menu is resident. Rename/copy/move/delete/
new-folder operations are not part of this preview; they require a later
TeensyROM firmware protocol extension.

SHA-256:

`b23bb6363f3a30fbb2154f7ef04381f64f559c3c9128ee85ee36f5e5277ab3c5`
