# MHS Desktop control system

The desktop uses a shared C64 bitmap control library in
`Source/C64/MainMenuCRT/source/GeosWidgets.s`. Browser, application, Control
Panel, Music, About, loading, file-operation and firmware-dialog callers draw
their content inside shared frames. They do not draw their own close glyphs
or simulate controls with punctuation.

This follows the useful separation in HamsterOS's `ui_controls` and
`ui_message_box`: shared geometry, drawing and input rules, with the application
owning the action. HamsterOS CGA's common window-frame routine follows the same
principle. The implementation here is native 6502 code and original C64 art.

## Geometry and drawing

All coordinates are integer pixels on a 320x200 high-resolution bitmap. A
rectangle is six bytes: X low, X high, Y, width low, width high, height. The same
rectangle drives drawing and hit testing. Callers must supply nonempty,
in-screen bounds.

- `UiFrame` clears its interior before drawing a one-pixel black keyline.
- `UiWindow` adds a separator 16 pixels below the top and a right-hand close
  button. The 11x11 close rectangle starts at X + width - 14, Y + 2.
- `UiClose` uses a dedicated seven-pixel diagonal X. It never uses the font's
  letter X or a PETSCII graphics character.
- `UiButton`, `UiCheckbox` and `UiScrollbar` share the same black-and-white
  control vocabulary. Selection is expressed in bitmap pixels, avoiding color
  leakage into adjacent labels or icons through an 8x8 VIC color cell.
- The 5x7 font includes distinct upper- and lowercase letters, digits and
  punctuation. Adding lowercase did not grow its 768-byte allocation.

Composition uses the existing protected canvas. `UiPublishRect` at `$c010`
publishes only the requested bitmap region, preserving partial-byte edge pixels
outside that rectangle, then publishes its color cells. Mouse/SID IRQs do not
borrow the library's drawing scratch or run the compositor. Existing resident
code and app memory limits remain enforced by assembly and build checks.

Menus preserve the base bitmap in the canvas. Opening, switching, selecting or
closing a dropdown restores its covered region from that canvas and draws the
transient menu directly on the visible bitmap. The private `RichAddressBias`
operand is restored to its normal canvas value before returning. These actions
never recapture browser labels or change directory selection. A full surface
change still composes and publishes its complete base before the menu overlay.

Drawing does not mask interrupts. Short mouse snapshots and decimal/TOD
operations remain atomic. The native mouse IRQ publishes pointer coordinates
without touching renderer or zero-page scratch, keeping the sprite responsive
while foreground drawing runs. Main-loop code still owns sprite visibility,
style and click dispatch. The SID IRQ restores the interrupted bank mapping.

## Dialog contract

`GeosDialog.s` owns confirmation, message, busy and cancellable-busy modes.
Callers provide their title, body and affirmative action; the dialog returns a
result. Firmware installation and file deletion remain explicit caller actions.

Cancel is the default for confirmations. Opening clicks and held keys must be
released first. A mouse action fires only when press and release hit the same
enabled control. Keyboard, joystick and mouse use the same selected action.
Busy operations do not display an active close control unless cancellation is
supported. Long filenames are streamed into wrapped text without losing case,
the dot or extension.

The firmware confirmation runs while the mouse sampling IRQ remains active.
The updater's existing interrupt shutdown and installation handshake begin
only after an affirmative result. The backend captures the source, directory,
name and file identity before displaying the question and validates them again
at launch. A changed selection or repeated launch cannot bypass confirmation.

Autolaunch, KERNAL/REU assignment, hotkey assignment, disk-mount confirmation
and NFC writing also use this dialog family. They preserve the existing backend
commands and show their results inside the bitmap window.

## File browser contract

The viewport contains four columns and four rows. Each 72x36 item region has a
24x16 icon and up to two lines of filename text. A proportional scrollbar
replaces desktop page gadgets. Arrow controls move one row; dragging changes the
visible range. IEC directory reads occur on committed movement, not on every
pointer sample.

Names retain their stored ASCII case and extension: `Text.txt`, not `TEXT TXT`.
Long labels use an ellipsis while retaining a short extension; dialogs retain
the full name. Label formatting never renames or changes the storage lookup.
Scrolling keeps a visible selection, updates its underlying raw index and name
together, and disarms double-click opening.

Text Viewer reuses the scrollbar with a 45-column, 17-line viewport. Its initial
count is bounded at 32,767 wrapped lines; a plus sign marks a capped count.
Committed scrolling reopens and skips to the requested line. Dragging only
updates the thumb until release, keeping sequential file reads out of the
pointer loop. The viewer remains read-only.

The new scrolling backend view is mode 2. Existing classic mode 0 and earlier
25-item mode 1 retain their protocol meanings. IO1 top/count registers expose
the new viewport without changing the raw file table.

## Preview and validation

Serve `docs/ui-preview` with a local HTTP server to open the interactive design
study. It uses the generated native font and desktop icons; the JavaScript
controls illustrate the design and are not a firmware emulator. Regenerate both
native and preview assets with `node scripts/generate-desktop-bitmap-assets.mjs`.
Its `--check` mode detects drift.

Run `node scripts/render-desktop-ui-proof.cjs` from the repository root for
native Home and browser PNGs in `build/ui-proof`. This assembles the production
6502 routines and executes them against deterministic RAM/IO fixtures; it
decodes the resulting bitmap and color cells, rather than recreating the drawing
algorithm. It does not model physical VIC timing.

Executed 6502 tests cover the native controls and publication boundaries;
backend tests cover viewport-to-file mapping and label preservation. Host tests
and a firmware build do not substitute for physical C64 mouse, video or cartridge
acceptance. The compact recovery menu and standalone advanced settings/help
programs retain their existing dedicated interfaces.
