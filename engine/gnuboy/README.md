# GBVM gnuboy adapter

Core source: Jean-Marc Harvengt's MCUME, `MCUME_teensy/teensygnuboy`,
commit `27f6b906aca34e06d6647bdca8215e25f8d20aa5`.
Original gnuboy authors: Laguna and Gilgamesh. GPL version 2 or later;
see COPYING and the retained upstream README. License/README reference:
https://github.com/rofl0r/gnuboy . The latter project's newer core was not imported.

MPE modifications, 2026-09-04: standalone callback-only adapter; no Arduino,
TFT, I2S, SD, heap, boot ROM, or copyrighted games included. Replaced the
256 KiB decoded tile cache with 512 bytes of scanline rows; fixed sprite-sort
overread, clipped negative window positions, fixed MBC5 bank zero, made memory
access portable and wrapped, exposed invalid opcodes as a stopping error.
DMG uses four neutral shades. CGB CPU double speed retains LCD/APU time.
The CPU returns actual elapsed half-dot units, including instruction overshoot.

GBVM links this core as a separate GPL program. Its complete corresponding
source, ABI interface, client sources, build scripts and this license travel
with every packaged binary. `node scripts/build-gb-core.mjs OUTPUT` rebuilds
the module using GNU Arm Embedded 11.3.1 (set MPE_ARM_PREFIX when needed).

Support is intentionally bounded: unbanked type 00, MBC1 types 01/02/03,
MBC5 types 19/1A/1B, ROMs up to 512 KiB, and either no cartridge RAM or
8 KiB RAM. September 5 adds SRAM backing and revision tracking; the module
owns battery persistence through generic host file services. Other RAM sizes,
RTC and other mappers are rejected. No save states, link cable, SGB effects,
or PCM fidelity claim.
Sound uses the four gnuboy APU channels and a three-voice SID approximation;
noise steals the wave voice. Original scanline/DMA/timer compatibility limits
remain. This is not an all-games or cycle-accurate emulator claim.

Desktop tests use the user's MARIO1.GB, Pac-Man.gbc, MARIO2.GB, Kirby and
ZELDA.GB locally. They are not
redistributed. Native rendered screenshots, input/picker tests and ARM builds
are evidence of integration, not physical Teensy/C64 gameplay acceptance.
