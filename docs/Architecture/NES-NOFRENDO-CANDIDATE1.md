# NESVM Nofrendo speed candidate 1 — 2026-09-04

The user measured the prior V1.1.5 module at **SPEED 35%, RUN 91%, HOST 9%**
and physically accepted F5's picture. RUN includes cartridge interrupts inside
the emulation pump; it is not a pure core-utilization profiler. The replacement
targets core work without changing MPE firmware, transport, F5 or the CPU clock.

## What changed

NESVM now uses the pinned MCUME Teensy41 Nofrendo CPU/PPU through a bounded
MPE adapter. CPU work advances by instructions and PPU work by scanlines,
instead of a 6502 pin tick plus three PPU dot ticks for every CPU cycle.
The existing ROM validation/mapper 0 and 11 policy, picker, held input,
APU-to-SID translation, emulation debt, frozen-frame ownership and generic
indexed video interface remain. Source-level core restrictions remain;
this is not an all-mapper or DMC implementation. Scanline accuracy tradeoffs
and local vendor fixes are documented in `engine/nofrendo/README.md`.

F1/F3/F5/F7 conversion remains wholly in MPE firmware. No GB-specific behavior
was added to NES or firmware. Later GB/GBC support can supply its own core
adapter and independent VM using the same services; it is not implemented yet.

## Evidence

- Equal-work desktop comparison: 17,898,000 emulated CPU cycles and the same
  ten-second input script per core. Crossbow ran 4.6–4.9x faster; local Popeye
  ran about 6.0x faster. These are **host core measurements, not Teensy rates**.
- Popeye old/new both produced 1,025 APU writes and 4,792 controller reads.
  Native 256x240 output snapshots were visually checked against the reference.
  Crossbow advances under both cores; counts differ with scanline/startup
  approximation, so broad cycle-accurate equivalence is not claimed.
- Captured vs discarded Nofrendo output has matching CPU RAM, APU registers,
  lengths, PPU RAM/palette/OAM, PC and frame count at the same cycle budget.
- New regression covers word/address wrap, RAM-mirrored DMA, controllers,
  palette buffering/mirroring, CHR-ROM write protection, scroll-buffer guards,
  exact credited cycles, long-run counter wrap and explicit DMC/JAM stops.
- Existing real module/host scheduler tests pass, including ROM-picker idle
  input, held/released keys, paging, Return/Fire, all four modes, immutable
  Busy retries, emulated-time debt and return-to-picker recovery.
- Full matched build/verification passes PAL/NTSC client and video checks,
  old core regressions, DOS checks, host memory/import/integrity gates and GUI
  tests. These are not hardware gameplay acceptance.

## Linked memory and installation

Candidate module: 94,020 bytes code (96 KiB limit), 15,360 bytes RAM1 static,
181,248 bytes available workspace. Native workspace high-water is 136,480
bytes and passes with only 163,840 bytes offered. Firmware host stack
reservation remains 49,152 bytes. No heap allocation was added.

`engine.mvm` SHA-256:
`bb8aa108bc16b1440e198512efa5a2bc6319aea4c1004d4c960ffcd5fb9ebaaf`.

The V1.1.5 firmware rebuilt byte-for-byte unchanged:
`a8a922090e9122c08060c33b2217c9ef48b09979a97abec466c3bb15e6fd8c46`.
If V1.1.5 is already installed, copy the new NESVM.zip contents to the SD root,
preserving private ROMs; **no new firmware flash is needed**. The picker title
includes `NESVM NOFRENDO`. DOS and AGI distributions are unchanged.

The ZIP includes the GNU Library GPL v2 license and complete corresponding
module source/linker/rebuild script. Publishing must rebuild those staged
sources to the exact released module hash before creating the ZIP. Only the
authorized Crossbow ROM is packaged; no private test ROM is copied.

## Required physical test

1. Confirm `NESVM NOFRENDO` appears and keyboard/joystick launch still works.
2. Run Mario and Popeye for at least 10 seconds, preferably using the already
   accepted F5 mode. Check movement and music cadence together.
3. Return+Shift to the picker; report SPEED, RUN and HOST. Target is near 100%
   emulated time, not a high display frame rate. Do not extrapolate the desktop
   ratio to hardware where cartridge interrupts also consume time.
4. Check F5 remains stable and centered, then F1/F3/F7, scrolling/sprite-zero
   splits, collision behavior and repeated game/picker transitions.

No claim of full hardware speed or broad game compatibility is made before
that run. If it remains slow, compare the new counters and profile cartridge
interrupt time separately; do not simply multiply the emulated NES clock.
