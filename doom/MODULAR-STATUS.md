# DoomVM: standalone port and 1 MiB audit

This page records the MCUME attempt. The subsequent
[GBADoom attempt and measurements](GBADOOM-STATUS.md) now have a working
bounded host run and pass the strict link using the RAM2 constant profile.
The measurements and failed fit gate below apply only to this MCUME attempt.

Updated 2026-09-04. Baseline: `cc14f287b167b584afb3470800a9231ad9545385`,
current shared ABI 2. This supersedes the required-8-MiB-PSRAM direction in
the historical Doom phase-1 documents.

**The first standalone extraction works in a host diagnostic, but does not
fit the current VM memory layout. No installable Doom module was produced.**
There is no firmware rebuild, flash, release replacement or physical C64
gameplay evidence from this work.

Subsequent core review: the user supplied Doom8088 and GBADoom. **GBADoom is
now the preferred next base**, subject to a complete ARM link and resource
backing test. Its compact design changes the sizing question; do not infer
that a new host flash profile is mandatory from the MCUME results alone.
See [low-memory core comparison](LOW-RAM-CORE-COMPARISON.md).

## What now exists

- [Standalone module](../vm/doom/doomvm.cpp): generic host callbacks, direct WAD
  launch, held input, one-gametic stepping, indexed video and silent SID
  frame-end packets. Frozen output waits for transport completion and ACK.
- [Platform adapter](../vm/doom/platform.cpp): bounded read-only WAD handles,
  offset reads, clock, recoverable startup errors and allocation metrics.
- [Reclaiming allocator](../vm/doom/heap.h): bounded RAM1 heap with coalescing
  free, preserving realloc, overflow checks and measured usage. The old
  reset-only allocator's no-op free is not reused.
- [Reproducible audit](../scripts/audit-doomvm.mjs): source-lock verification,
  ARM measurement and strict link, 32-bit real-core probes, negative memory
  tests and provenance. The existing firmware/ABI limits remain enforced.

MCUME remains pinned to `27f6b906aca34e06d6647bdca8215e25f8d20aa5`.
The supplied shareware v1.9 WAD is 4,196,020 bytes, SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
Neither upstream source nor WAD is newly vendored by this work.

## Measured memory and execution

The exact final values, tool output and input hashes live in the ignored
`build/doom/modular-audit/report.json` and adjacent maps. Final audit
measurements are:

| Allocation | Measured requirement | Current ABI-2 allowance |
| --- | ---: | ---: |
| Module executable text | 162,200 bytes | Shares a 98,304-byte code window |
| Read-only tables/constants | 123,492 bytes | Same 98,304-byte window |
| Static module data/BSS | 262,752 bytes | 196,608 bytes, including all support |
| Additional private heap high water | 117,336 bytes in the E1M1 host diagnostic | Must also fit the support window |
| Host indexed-video workspace | 24,576 bytes | Must also fit the support window |
| Doom zone | 524,288 bytes in the bounded test | Exclusive 524,288-byte RAM2 |

The private heap includes the 64,000-byte framebuffer; it is not another
allocation to add again. Host-library behavior is not identical to ARM newlib,
so its high water is host evidence, not a target-stack or hardware proof.
The static ARM overflow already rejects this candidate before any heap exists.
The constrained entrypoint therefore receives zero remaining workspace and
rejects startup; the oversized diagnostics must not be interpreted as 1 MiB
execution.

In the real-core diagnostic, E1M1 ran for **140 gametics** with a 512 KiB zone,
producing **132 changing frame transitions**. The final frame hash matched the
8 MiB-zone control. This is approximately four seconds of selected movement
and fire, not completion of the level or proof that every scene fits.
Zone allocation high water was **519,768 bytes**, including allocation block
headers and purgeable cache, excluding the 32-byte zone header. The 8 MiB-zone
control peaked at 2,194,012 bytes because it retained more cached resources.
The smaller zone incurred 1,287 WAD reads / 3,303,074 bytes versus 831 reads /
1,936,322 bytes in the larger-zone control. Resource purging trades RAM for
storage traffic; physical SD and display timing still need measurement.

A separate oversized-RAM1 diagnostic exercised the actual module callbacks
through 140 indexed-video frames, including Busy/retry, unchanged frozen pixels,
frame-end packets, withheld ACK and input press/release. Heap and file adapter
tests, arena-end guards, handle closure and deliberate allocation failures are
checked by the same audit. None of these host probes executes the Cortex-M7
ELF or the physical C64 presentation path.

## What the next implementation must change

The zone does not need to remain fixed at 8 MiB for this initial scene. The
remaining work is principally module code placement and RAM1 support storage:

1. **Provide a generic flash-backed module profile, or substantially replace
   the core.** Even executable text alone exceeds the current 96 KiB window.
   Read-only tables need separate placement. A flash profile must be a generic
   host service, with a measured slot outside firmware/updater ranges, safe
   installation, cache coherency and reset recovery; no Doom-in-firmware
   fallback. The existing RAM-only ABI is not silently enlarged by the audit.
2. **Reduce support data before designing the final partition.** Large ARM
   objects include `visplanes` (84,992 bytes), `openings` (40,960), `states`
   (27,076), `mobjinfo` (12,604), `drawsegs` (12,288) and `zlight` (8,192).
   Code placement alone does not fix those plus the framebuffer and private
   heap. Const-splitting and compact renderer structures need actual changes.
3. **Use a renderer designed for a small memory budget.** Simply changing
   `SCREENWIDTH`/`SCREENHEIGHT` is insufficient: menus, status patches and
   intermission drawing assume the original canvas. Avoid a clipped or unsafe
   demo created by lowering constants or array limits. Evaluate the compact
   representations in [RP2040 Doom's RAM design](https://kilograham.github.io/rp2040-doom/speed_and_ram.html)
   and [flash-backed assets](https://kilograham.github.io/rp2040-doom/flash.html)
   against this host. That author's port moves immutable data to flash and
   changes object widths/layouts extensively; its hardware result does not
   prove a Teensy/C64 port.
4. **Repeat the real fit gate**, accounting separately for generic host code,
   module code, DTCM support, private heap, the 48 KiB stack reservation and
   exclusive RAM2. No PSRAM or oversized diagnostic arena may satisfy this gate.
5. Once it fits, complete picker/client/input/audio and test E1M1 on physical
   hardware. Required controls include W/S forward/back, cursor left/right
   turning, A/D strafe, Control fire, Space use, Shift run, Tab map and weapons
   1-7; test simultaneous held actions, release, queue overflow and C64
   keyboard ghosting. The current single-scan client does not prove that gate.

Run instructions and exact current scope:
[vm/doom/README.md](../vm/doom/README.md).
