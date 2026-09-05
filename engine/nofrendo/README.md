# Nofrendo MPE adapter (2026-09-04)

CPU/PPU source: Jean-Marc Harvengt's MCUME, `MCUME_teensy41/teensynofrendo`,
pinned commit `27f6b906aca34e06d6647bdca8215e25f8d20aa5`.
Upstream: https://github.com/Jean-MarcHarvengt/MCUME/tree/27f6b906aca34e06d6647bdca8215e25f8d20aa5/MCUME_teensy41/teensynofrendo

Nofrendo copyright 1998-2000 Matthew Conte; GNU **Library** General Public
License version 2, in [COPYING](COPYING). Original legends are retained.
`machine.cpp` / `machine.h` are the MPE adapter, also under that license.
The unmodified imported headers are `bitmap.h`, `log.h`, `noftypes.h` and
`nes_ppu.h`; local changes to the other imported files are listed below.

## Local port changes

- `nes6502.c`: RAM-mirrored/16-bit wrapped instruction reads, zero-page word
  wrap, byte-safe cross-bank word fetches, mapped DMA reads, unsigned cycle
  accounting. Instruction-based CPU execution remains upstream Nofrendo.
- `nes6502.h`: unsigned elapsed-cycle counter avoids signed long-run overflow.
- `nes_ppu.c`: MPE guards exclude standalone allocation, drivers, input,
  palette setup and debug viewers; bounded 1 KiB page pointers; eight leading
  scanline guard pixels; wrap-safe sprite-zero comparison; OAMDATA reads.
- Adapter owns singleton lifecycle, safe CHR-ROM writes, palette RAM/read
  buffering, mapper 0/11 banking with existing bus conflicts, DMA, controller
  serialization, APU/SID state, NMI edges, and instruction-overrun credit.
  A run never returns more cycles than requested. All PPU scanlines are
  evaluated even while a presentation frame is frozen; no fake sprite-zero
  shortcut changes gameplay with video backpressure.

This is a scanline-approximate core, not a cycle-accurate replacement. CPU I/O
within a line, NMI races, odd-frame dot skip and mid-line effects are not all
exact. Existing NTSC mapper 0/11 restrictions and explicit unsupported-DMC
error remain. Existing SID sound is retained; MCUME's I2S driver/APU mixer is
not imported. The old chips/dot core remains a test reference, not linked
into the released NES module. No private ROM is included in source packages.

## Rebuilding or modifying the linked library

NESVM downloads include the complete module source under `SOURCE/`, this
license, the linker script and `scripts/build-nes-core.mjs`. Install Node.js
and GNU Arm Embedded 11.3.1 (the Teensy 1.61 compiler). Set `MPE_ARM_PREFIX`
to the absolute compiler prefix ending in `arm-none-eabi-`, then run:

```
node scripts/build-nes-core.mjs build/relinked
```

From the extracted `SOURCE/` root, this rebuilds `build/relinked/engine.mvm`.
Use that file with the supplied client and compatible MPE V1.1.5 firmware.
No signing key, firmware relink, ROM or hardware flashing is required to
rebuild. Modification and reverse engineering for debugging modifications
to this library are permitted under its license. Full repository tests:
`vm/tests/nofrendo_test.cpp`, `module_test.cpp`, `nes_timing_test.cpp`.

## GB / GBC extension boundary

Nofrendo is only an NES implementation inside NESVM. It must not enter the
firmware or become a GB API. A later GB/GBC core (for example gnuboy, after
its own license/fit review) can be a separate VM using the same generic MPE
clock, yielding, borrowed memory, files, held input, indexed video and sound
services. Each core adapter owns its native timing, palette, controller
mapping and sound translation. Firmware continues to own presentation-mode
selection, C64 transport and physical I/O. Battery saves and GB sound fidelity
remain future work; no GB/GBC support is claimed by this release.
