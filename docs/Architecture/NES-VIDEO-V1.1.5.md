# V1.1.5: stable enhanced updates and speed evidence

This release changes the generic MPE indexed video service and its opt-in NES
client. No game identities are inspected. DOS/AGI downloads are unchanged;
AGI keeps its native solution. No player-sprite overlay is implemented.

## F3/F5 update correction

Previous enhanced frames explicitly cleared DEN for every upload. The new
client advertises border-stream support (START timing $82 PAL / $83 NTSC).
After the initial legacy setup, firmware uploads only the inactive image,
using grants emitted on raster lines 251/252. Stale grants over 500 us are
discarded. Burst limits are 3200 bytes PAL / 1600 NTSC, with additional
wall-time checks between at-most-400-byte segments. CPU emulation runs between
bursts. Completion sends a flip request; the client switches VIC bank and
kernel in the border and ACKs only after the flip. No steady-frame DEN clear.

| Buffer | Bitmap | Upper/lower screen | Raster kernel |
| --- | --- | --- | --- |
| 0 | $6000 | $5c00 / $5800 | $3000 |
| 1 | $a000 | $8c00 / $8800 | $c000 |

The second screen maps deliberately avoid the VIC character-ROM shadow at
$9000-$9fff. Kernels use shared exact-cycle delay routines and fit within
4 KiB each. The active bitmap, screen maps and kernel remain unchanged during
an upload. Legacy CELL replacements invalidate host bank ownership, so return
to picker and relaunch initialize correctly. New client and firmware must be
installed together. First setup and transitions to plain F1/F7 still pause.

F5 now centers 256 source columns at 32..287, like F7. This also places NES
content outside the leftmost 24-pixel FLI artifact; it does not remove the VIC
artifact itself. F3 retains its existing full-width fit. Vertical 240-to-200
fitting and all four Commodore+Control+unshifted function-key mappings remain.

## Remaining speed problem

The user reports V1.1.4 at roughly one-third speed in all modes, including
stationary title music. Its synthetic timing-policy tests do not prove real
Teensy throughput. This release does not claim a measured 3x speedup.

NESVM now places its frequently accessed 4384-byte CPU RAM/nametable/palette/
OAM block in tightly-coupled RAM1 instead of cached RAM2. ROMs remain in RAM2.
Allocation is bounded by the actual module-lent workspace; no host heap or
stack reservation is reclaimed. No CPU/PPU/APU time is skipped or overclocked.

A lightweight two-second hardware-clock sample appears on the picker after
returning from a game: `SPEED ...% RUN ...% HOST ...%`.

- SPEED = actual emulated CPU cycles divided by expected NTSC cycles.
- RUN = wall time inside the emulation pump, including interrupts during it.
- HOST = the remainder, including video conversion, DMA and packet handling.

RUN is not a pure CPU utilization counter. This readout distinguishes an
emulation/interrupt throughput limit from time predominantly outside the core
without writing files, adding overlays, or changing VM firmware ownership.

## Acceptance

Software checks cover converter geometry, all split positions, bounded kernels,
frozen generation/ACK ownership, inactive-bank uploads and DMA failure release,
late grants, mode transitions, picker recovery, measured-counter arithmetic,
and real PAL/NTSC VICE bank flips/raster colors without DEN blanking. VICE does
not emulate this physical Teensy DMA bus; hardware burst timing is still a gate.

Install V1.1.5 firmware plus the complete NESVM.zip, preserving private ROMs.
Test F5 moving/scrolling for 30 seconds, F3, then F1/F7 and picker/relaunch.
For speed, play at least 10 seconds in F7, press Return+Shift (Start+Select),
and report the SPEED/RUN/HOST line. Repeat F5 if needed. Confirm music tempo,
input and absence of whole-screen flashes on the actual PAL/NTSC setup.
