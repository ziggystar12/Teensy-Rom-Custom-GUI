# TeensyROM Custom GUI

![TeensyROM monochrome desktop preview](docs/mockup/teensyrom-desktop-preview.png)

This public TeensyROM+ firmware project combines two related tracks:

- a true 320x200 standard high-resolution VIC-II bitmap desktop with mouse,
  keyboard, and joystick parity; and
- the MHS Power Engine acceleration work, including the current AGI-64
  reference integration and the reusable firmware services intended for other
  projects.

The project is based on
[SensoriumEmbedded/TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM)
at commit `3436b8fbd7c642ef9eabc691d3d09da08a6a6690`. The upstream MIT license and
history are retained.

## Desktop preview

The current GUI build is described in
[`firmware/FILE-OPERATIONS.md`](firmware/FILE-OPERATIONS.md), with checksums in
[`firmware/`](firmware/README.md). The native07 MHS Power Engine kit is a
separate release paired with GUI revision `e305f6d`; it does not contain these
new file operations.

The C64-side desktop provides:

- native standard high-resolution bitmap rendering: one bit per pixel, with a
  foreground/background color pair for each 8x8 cell (not multicolor mode);
- the mockup's pixel-drawn icons, six-pixel-spaced font, dotted desktop, and
  outlined menus, with two-line filenames (up to 20 characters);
- a Commodore 1351 mouse on control port 1;
- a joystick on control port 2;
- complete keyboard operation when no mouse is attached;
- folder, disk-image, program, and document icons;
- selection and opening by mouse, joystick, or keyboard; and
- the compact cartridge and classic list view as character-mode recovery
  paths.

The Teensy menu also launches three resident high-resolution black-and-white
demo apps: Snake, an integer Calculator, and a paged Text Viewer. Their drawn
close button or STOP returns to the desktop without a cartridge reset. Text
files opened from Teensy/SD/USB use the bitmap viewer while icon view is active.

Open [`docs/mockup/index.html`](docs/mockup/index.html) locally for the
interactive design preview. The implemented desktop shell adds the clickable
menu bar, a clock-adjacent SID play/pause control, Control Panel routing,
movable and persistent top-level icons, real Drive 8/9 directory browsing and
PRG launching, and a single-window icon browser described in
[`docs/CUSTOM-DESKTOP.md`](docs/CUSTOM-DESKTOP.md). Copy, Paste, and permanent
Delete work on individual files in SD and USB folders. Paste never overwrites
an existing file and verifies its copy before publishing it. Delete displays
the captured filename and starts with Cancel selected. There is no Trash icon,
persistent clipboard, or recovery store. Folder operations, disk-image contents,
and IEC writes remain unsupported.

File windows have pixel-drawn close, up, and page-arrow gadgets, with a framed
title/path area. Colors are staged until the new bitmap is drawn, so entering
a drive does not recolor the old desktop before its icons are replaced.

## Acceleration architecture

The TeensyROM+ firmware owns the safe mailbox, capability negotiation, bounded
decode, cache/prefetch helpers, DMA transfer lifecycle, deadlines, and
fail-closed recovery. AGI-64 is currently the reference client and supplies
the matching cartridge layout, resource metadata, and C64 fallback behavior.

The long-term boundary is deliberate: generic transport and acceleration stay
in this repository, while engine-specific adapters may live in AGI-64 or other
client projects. See
[`docs/Architecture/GENERIC-ACCELERATION.md`](docs/Architecture/GENERIC-ACCELERATION.md)
and the detailed protocol-v3 handoff for the present capability set.

## Source layout

- `Source/Teensy/MinimalBoot/` - TeensyROM+ acceleration firmware and mailbox
- `Source/C64/MainMenuCRT/` - monochrome desktop, input handling, and source tests
- `docs/Architecture/` - generic firmware architecture and AGI-64 integration
- `patches/` - ordered patches against the pinned upstream commit
- `firmware/` - latest combined experimental firmware and checksum

## Focused verification

From the repository root:

```powershell
node Source\Teensy\MinimalBoot\tests\agi-picture-conformance.mjs
node --test Source/C64/MainMenuCRT/tests/*.test.js Source/C64/MainMenuCRT/tests/*.test.mjs
node scripts/generate-desktop-bitmap-assets.mjs --check
```

Rebuild the C64 menu before building firmware so the compact bootstrap cartridge
and `DesktopShell.prg.h` both contain the current C64 code:

```powershell
.\scripts\build-c64-menu.ps1 -AcmePath C:\path\to\acme.exe
```

The script accepts `-PythonPath` or uses `python` from PATH. Then build
`Source/Teensy/tools/Build-DualBoot.ps1 -Fab04_Features` with the configured
Arduino/Teensy toolchain. Install the matching complete firmware image; these
file operations require the new C64 menu and Teensy backend together.
The complete upstream usage and build documentation remains available in the
[original TeensyROM repository](https://github.com/SensoriumEmbedded/TeensyROM).

## Hardware status

The GUI firmware is an experimental TeensyROM+ Fab0.4 build. Host regression
tests and assembly/build checks do not establish physical C64/128, SD/USB,
or 1351 mouse acceptance. File operations still need real-hardware testing.
The official restore firmware remains available in `firmware/`.

No Sierra game data, AGI game files, or AGI-64 compiler binaries are included.
