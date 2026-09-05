# GBVM battery cartridges — September 5, 2026

Module-only update on existing V1.1.7; no firmware or other VM binary changes.
The ROM ceiling remains 512 KiB. No paging, PSRAM or extra flash is assumed.

The supplied Mario 2 and ZELDA.GB headers both describe 512 KiB MBC1+RAM+battery
cartridges with 8 KiB SRAM (type 03, RAM code 02). The old loader rejected
these deliberately, even though the ROMs fit RAM2. Kirby's supplied 256 KiB
MBC1/no-RAM ROM was already accepted and passed the unmodified core smoke test.
The user's ZeldaDX.gb is 1 MiB with 32 KiB RAM and remains unsupported.

The core now backs 8 KiB SRAM, gates it on the mapper RAM-enable register,
tracks changed bytes and exposes battery data without storage calls. Supported
RAM profiles extend MBC1 to 02/03 and MBC5 to 1A/1B. Bank/RAM-enable semantics
are checked against [Pan Docs MBC1](https://github.com/gbdev/pandocs/blob/master/src/MBC1.md)
and exercised with synthetic cartridges; this is not all-MBC/game support.

GBVM stores alternating checked snapshots under SAVES, keyed by ROM CRC32.
It writes the inactive slot, flushes/closes, reopens and verifies length,
identity, sequence and CRC before treating that slot as committed. The last
verified slot is never truncated. Both invalid slots refuse loading. Save
errors retain live SRAM and require a retry before loading a different ROM.
Read-only ROMs and pre-existing external .sav files are never modified.

RAM1 ARM layout: 69,592 code bytes; 2,048 data; 157,344 BSS. There are 37,216
workspace bytes remaining, of which save validation borrows 8,192. Guest ROM
backing remains the full 524,288-byte RAM2. Host stack reservations are separate;
no heap or hidden expanded cartridge buffer is introduced. No unresolved
imports or dynamic constructors; packaged source relinks to the exact module.

Tests:

- Synthetic MBC1/MBC5 bank aliases, 8 KiB RAM enable/reset, battery flags,
  unchanged-write revision, unsupported RAM/ROM/RTC and checksum rejection.
- Actual local Mario 1, Pac-Man GBC, Mario 2, Kirby and Zelda: 12 emulated
  seconds with input, varied native frames, sound generation and no core fault.
- Module picker and direct-loader runs, frozen video while Busy, emulated-clock
  accounting, four DMG shades, no runtime file I/O on non-battery carts.
- Mario 2 and Zelda battery save/reload and isolation; injected write/flush
  failure, corrupted-newest-slot fallback, both-corrupt rejection, failed-exit
  retry, oversize rejection and scratch-memory guards.
- Rebuilt/relinked GPL source and ZIP member hashes. The rebuilt client picks
  up V1.1.7's compatible three/four-byte video control receiver; PAL/NTSC boot
  and enhanced raster tests check that client separately. No converter changes.

The actual test ROMs are read locally and are excluded from Git and ZIPs.
Generated evidence lives in build directories. No physical Teensy/C64 speed,
sound, save-durability or complete-game acceptance is claimed. Test the three
requested games on hardware, return with Start+Select, relaunch to check saves,
then reset only after successful return to the picker.
