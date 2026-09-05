# NESVM timing correction — V1.1.4 / vm-test-5

The user reports roughly quarter-speed gameplay AND music on hardware. This
release corrects reproduced scheduling faults; modeled timing is not a claim
that physical Mario gameplay has now been measured at full speed.

## Faults and corrections

- The host consumed an ACK, then artificially expired the new 1.5 ms pump
  slice. NES repeatedly sent unchanged SID packets. A promptly acknowledging
  client could therefore keep the game clock starved indefinitely. ACK is
  still consumed before pumping (preserving the picker fix), but every turn
  now gets one normal bounded slice. Input/quiet/new ACK still cause a yield.
- NES started another blocking conversion/transfer even when its CPU clock
  was behind. It now catches up before beginning a new indexed submission;
  an in-flight submission is still drained. The immutable captured frame is
  retained and changed sound may be sent while it waits. Catch-up waiting is
  limited to 100 ms per captured image, so a core that cannot keep up cannot
  starve display/input indefinitely. This fallback does not claim full speed.
- The 50 ms debt cap discarded elapsed CPU/PPU/APU time. Debt now uses a
  64-bit accumulator without that cap. Catch-up remains cooperatively bounded;
  it does not skip CPU instructions, PPU dots, APU ticks, or controller time.
- Unchanged gameplay SID heartbeats are removed. Acknowledged note triggers
  are cleared only if no newer audio revision has replaced them. The menu's
  input-driving frame-end heartbeat remains intact.
- Unpresented frames no longer compose every pixel or scan all eight sprites
  for a result nobody consumes. They retain sprite-0 collision tests where
  needed, and retain all fetch/scroll/NMI/CPU/APU work. This is not a fast-forward
  clock multiplier, game-specific patch, or omitted emulation frame.

## Reproduction and regression

`vm/tests/nes_timing_test.cpp` uses the actual NES module and firmware polling
code. Only clock, files and transport are modeled. With prompt ACKs and two
modeled seconds, the pre-fix code completed 30,426 CPU cycles and one PPU frame.
With the fix it completes exactly 3,579,546 cycles and 120 PPU frames.

A second scenario charges 1 us per eight emulated CPU cycles and 40 ms for
each video transfer. The ACK-only fix delivered 68.5% of real-time guest
progress in that model; catch-up-first presentation delivers approximately
98%. This is a deterministic contention regression, NOT a Teensy benchmark.
A 250 ms pause additionally verifies that elapsed emulated time is retained.

Tests also cover picker held/release/paging/launch/return, immutable pending
frames, absence of duplicate SID heartbeats, and every-dot PPU-state equality
with/without a pixel destination across clipping, sprite flips and positions.
The complete matched firmware/module build and verification are required
before the tracked download is replaced.

## Release verification

The matched build and complete `scripts/verify-vm-test.mjs` suite passed using
`MPE_VM_TEST_OUT=build/vt`. This includes the real module/scheduler regressions,
428,370 synthetic core checks, PAL/NTSC client/raster checks, DOS/Tandy and
file-service compatibility, image CRC/bounds, and the 48 KiB host stack bound.
NES module code is 86,492 bytes with 3,200 bytes of static RAM1 state.
The generated NES ZIP was reopened and every packaged member hash verified;
only the authorized Crossbow ROM is included. DOS/AGI downloads were not replaced.

- Firmware SHA-256: `f4da2046f6c172e51020eb313dac4347bc6717c942d77640306a0f806a8fd864`.
- NES module SHA-256: `69ecabdc22748dfef65c355227d355ad436c6edf55f06b4c034e9051cdb33f84`.
- NESVM.zip SHA-256: `42b6e7e69e7c2dd31334dfb70d1017239d0a717d6431c849643c8204a8ba7907`.

## Install and physical check

Install BOTH `MPE_Firmware-V1.1.4.hex` and the updated `NESVM.zip`, preserving
private ROMs. Reboot and confirm V1.1.4 in About. DOS/AGI packages need no update.
The ROM picker fix and centered native-width F7 are included. F1 remains the
default; F3/F5 enhanced-mode blanking and left-edge artifacts are NOT fixed.

First test the same Mario scene in F1 or F7. Compare countdown progression,
walking speed and music tempo with normal NES playback over a timed interval;
check held joystick/keyboard input and return to the picker. Test PAL/NTSC and
C128-in-C64-mode as available. Do not infer full speed from a static image or
from this host model. No hardware flashing is performed by the build scripts.
