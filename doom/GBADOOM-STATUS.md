# GBADoom E1M1 RAM-only VM candidate

The standalone ARM module now links inside the Teensy's 1 MiB internal RAM
layout. E1M1 runs in the 32-bit host harness with a 416 KiB game arena, streamed
SD assets and three synthesized SID effect voices. Physical gameplay and sound
have not yet been tested. This is a local test candidate, not a released port.

The source is pinned to [GBADoom](https://github.com/doomhack/GBADoom/tree/89097b3ff31ac1e1b2cdce9854e49726cfa462bf).
Only the shareware demo's first level is supported. New-game requests are pinned
to E1M1; normal and secret exits restart E1M1 without an intermission or E1M2.

## Memory placement

| Reservation | Bytes | Use |
| --- | ---: | --- |
| RAM1 host ITCM ceiling | 98,304 | Existing generic host |
| RAM1 module ITCM | 98,304 | 94,888 bytes of executable code/API |
| RAM1 host data/heap ceiling | 81,920 | Existing host reservation |
| RAM1 module data/BSS/workspace | 196,608 | 128,576 static + 68,032 support workspace |
| RAM1 execution/interrupt stack | 49,152 | Existing reserved stack |
| RAM2 guest arena | 425,984 | 416 KiB game state and bounded resource cache |
| RAM2 constants | 98,304 | 93,984 initialized bytes, MPU read-only and non-executable |
| **Total reservations** | **1,048,576** | **No PSRAM or module flash writes** |

The module's RAM1 initialized data includes 65,024 bytes of fixed tables.
Support high water in the host run is 48,976 bytes, including the 24,576-byte
indexed-video workspace and allocator overhead, leaving 19,056 bytes there.
The 416 KiB zone high water is 416,544 bytes including allocation headers;
its separate 20-byte zone header leaves 9,420 bytes at that measured peak.
These figures are bounded test observations, not worst-case whole-level proof.
The 48 KiB stack is a reservation; full ARM stack high water remains unmeasured.

Profile 1 uses two formerly reserved MVM1 header words and a required service
bit. The loader validates bounds and checksums over code, data and RAM2 tables
before entering the module. Existing profile-0 VMs still receive 512 KiB RAM2.
Older firmware rejects profile 1, so this module needs the matching test host.

## Evidence

- Strict Cortex-M7 link passes with no unresolved imports or raw heap calls.
- All 65,537 reciprocal-table entries match the compact integer replacement.
- 140 ABI frames test simultaneous movement/fire, release, indexed Busy/retry,
  withheld ACK and immutable pixels, palette and SID payloads.
- The 416 KiB and forced-purge 384 KiB movement/turn/fire runs match the oversized
  diagnostic control's final image and complete sound-packet hash.
- 2,100 game tics exercise repeated normal/secret exit and new-game requests;
  every tic remains E1M1 and matches the oversized control's image/sound results.
  Exit calls are injected by the harness, not a demonstrated playthrough.
- SID tests cover shots, doors, pickups, three simultaneous voices, volume,
  pitch slides, retriggering, explicit stop and bounded lifetime for every ID.
- Zone/support exhaustion, raw WAD, checksum, directory bounds and I/O errors
  return cleanly with file handles closed and memory guards intact.
- The shared loader test checks legacy/profile headers, short reads, CRC,
  segment bounds and isolation between the guest arena and constant tables.

ARM linking, host execution, C64 emulator checks and physical acceptance are
separate claims. Generated reports record their respective results.

## Sound and remaining hardware work

Doom's sampled sound events become short SID noise/pulse/triangle/saw effects.
This uses the same gate-mask plus 25-register output as DOSVM's Tandy-to-SID
path. It does not emulate a Tandy chip or reproduce the original recordings.
Music, PCM playback and persistent saves are not implemented. Pitch values
use the existing NTSC-oriented convention; PAL/NTSC listening needs tuning.

Physical acceptance must cover launch and reset, RAM2 MPU/cache behavior, SD
latency, view changes, held/released keyboard and joystick chords, sound and
extended E1M1 play. A passing host run cannot establish playable hardware speed.

See [build and controls](../vm/doom/gba/README.md) and [asset licensing](LICENSE.md).
All local artifacts remain under ignored build/doom; no flash or publication
is performed by the build scripts.

## Completed local kit verification

The matching generic host links with 90,280 bytes of ITCM code, a 16,384-byte
host heap ending at 0x20010980, no RAM2 static allocation and the unchanged
49,152-byte stack reservation. The E1M1 module is 254,152 bytes on SD; this
includes initialized tables and does not represent its entire RAM footprint.

The focused verification passed legacy NES/DOS image checks, actual DOS boot
with its full 512 KiB guest arena, indexed-video checks and both legacy and
profile-1 registry launch/CRC checks. VICE booted the actual Doom CRT in PAL
and NTSC and applied a real core-generated SID packet, followed by silence.
This proves register delivery in the C64 emulator, not physical audible quality.

The local kit and evidence are in build/doom/e1m1-test: README.txt, SD/,
package.json and verification.json. Firmware was built but not flashed.
