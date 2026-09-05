# NES fast DMA baseline and picker input correction

September 4, 2026. User reported that both the port-2 joystick and keyboard
cursor keys could not select a game, and Return could not launch one.

Two independent causes were found:

- The idle module sent audio-only SID packets, without the frame-end bit.
  The client captures held input in its raster handler but sends queued input
  between frame ends. The idle picker therefore never delivered those events.
- The NES-specific client mapped joystick directions but not keyboard cursors.

The menu now emits paced, zero-cell frame ends. Gameplay audio-only service,
video DMA and pending-frame ownership remain unchanged. Cursor Down/Right now
map to NES Down/Right; either Shift produces Up/Left. A Shift used for a cursor
chord does not also assert Select when the cursor key is released first.
Standalone Shift retains Select; Space remains B, Return Start and port-2 Fire A.

## Main-branch downloads

The fast firmware and corrected NES package are promoted to the normal
[firmware download](../../firmware/README.md) and [NESVM.zip](../../vms/NESVM.zip).
The firmware remains byte-for-byte the issued fast-test image:
`90dbbce97b5e40b4e77c37902e2407711ef6f36c1421c15c7aaac48d28991a8b`.
Existing fast-test users do not need to reflash. Users with the older pre-DMA
V1.1.1 download must install the current HEX; the About label alone is ambiguous.

The generic ABI-2 tail extension provides synchronous VIC cell DMA transport.
NES requires service mask 55 (files, clock, packets, guest RAM, video), while
the firmware provides 63 including writable storage. Base DOS modules retain
their prefix-size check. The module prepares frames, emulates the NES and owns
display policy; the firmware does not contain a NES engine. RAM1 remains code
and support, with all 524,288 RAM2 bytes reserved for the guest arena.

The NES core uses optimized compilation and avoids redundant foreground pumps.
DMA replaces full cell-packet transfers when available; the acknowledged packet
fallback remains covered. Neither host tests nor this promotion establishes
physical SMB performance. New indexed-video modes are a follow-on release.

## Earlier input-only update

`build/nes-picker-fix-20260904/NESVM-PICKER-UPDATE.zip` targets the already-issued
`build/nes-fast-test-20260904-1832` firmware candidate, not the older public
pre-DMA V1.1.1 firmware or the later indexed-video work in progress. It contains only
`NESVM.crt`, `VMS/NESVM/client.crt`, `VMS/NESVM/engine.mvm` and instructions.
Extract to SD root, replace those three files and reset before launching.
No firmware, ROMs, manifest or other VM files are replaced.

The fix is also present in the ongoing indexed-video source. Its different
input protocol/video selectors were preserved. That next candidate has its
own matched-firmware requirement and is not silently included in this update.

## Verification

- A fresh full isolated firmware/NES/DOS build and `verify-vm-test.mjs` pass.
  The rebuilt HEX matches the issued fast-test image byte-for-byte. Generic
  storage, DOS C:/D: saves/Tandy, both client PAL/NTSC checks, image bounds,
  engine-free firmware linkage and the 49,152-byte host stack budget pass.
- Published NES engine SHA-256:
  `6454010ae2b4463633f86fabf4cec3bdf5aa2e3412cc88520b2f9603406c26ef`.
  Published client SHA-256:
  `58f04ad65162e2d3ab0ecc5f75a89031208b793b058e12ca691be186a5800ee5`.
- The native idle-frame regression failed before the fix.
- Actual module tests pass: four idle frame ends with no cells/replacement,
  held/released directions, 17-row paging, a 77-cell row change, Return and
  Fire launch, game-to-picker idle recovery, and exact direct-file selection.
- Crossbow runs 120 presented frames in the host harness; immutable pending
  frames, Busy/Unavailable video fallback and real mode replacement still pass.
- `nes/tests/picker_idle.mjs` runs the exact C64 client main loop against captured
  native menu packets, changes matrix/joystick inputs after the menu appears,
  and verifies 29 input transitions without hiding the screen. It covers Shift
  release order, held inputs, keyboard diagonal plus Fire, opposed joystick
  directions and the original fast candidate's Sharp toggle.
- Module integrity/bounds and actual CRT reset/START/timeout checks pass in
  PAL and NTSC VICE. No physical C64/Teensy input or gameplay timing claim.

The input-only build was isolated at `E:/MHS-NES-Input-725a/gui` to avoid mixing
concurrent indexed-video changes into the user's installed fast candidate.
Recovered baseline module/engine sources match its recorded input hashes;
the isolated build embeds a shorter assertion file path. Its original client
rebuild matched the fast-test CRT byte-for-byte before adding cursor support.
The source build script, tested sources and verification files are preserved
beside the update. No existing fast-test kit is overwritten.

Hardware acceptance: reset, open NESVM.crt, wait idle, navigate with both input
devices, page left/right, launch with Return and Fire, then return with
Start+Select and confirm the picker still responds. Release held buttons/keys
between selections. Recheck Sharp and ordinary gameplay controls.
