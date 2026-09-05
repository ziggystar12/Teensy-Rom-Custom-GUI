# Doom phase 1 status

Updated: 2026-09-04.

Historical checkpoint. The current [GBADoom E1M1 candidate](GBADOOM-STATUS.md)
links within the internal-RAM-only VM layout and passes bounded host tests.
It supersedes the mandatory 8 MiB PSRAM assumption below. The earlier
[MCUME extraction](MODULAR-STATUS.md) remains comparison evidence.

## Outcome so far

The native Doom direction is viable enough to continue. The pinned MCUME
Teensy Doom core now has a reproducible host proof that loads the shareware
v1.9 WAD, initializes E1M1, stays in `GS_LEVEL` for 420 iterations, accepts
movement/fire input, and produces continuously changing 320x200 indexed
frames. A separate MHS converter consumes a real frame from that run and emits
the existing 12-byte VIC-II cell records for a complete 160x200 multicolor
view.

This is a software proof, not a playable TeensyROM build. The upstream core is
not yet linked into firmware, no C64 Doom launcher/presenter exists, sound is
not connected, and no physical C64/Teensy timing or gameplay claim has been
made.

## Reproducible evidence

| Check | Result | Acceptance boundary |
| --- | --- | --- |
| MCUME source lock | 184 files, 1,605,084 bytes, inventory SHA-256 `91e9d8c5bac42aff37756b7566ddbf92b5e6cc5500761a9474c9da3a64922ffb` | Exact source identity only |
| Real Doom host run | Two deterministic 32-bit runs; E1M1 `GS_LEVEL` 420/420; 327 changing transitions; 324 unique frame hashes | Host execution only |
| Doom runtime | Held keyboard and joystick actions, ordered make/break events, wrap-safe 35 Hz scheduling, bounded catch-up | Host tests plus Cortex-M7 compilation |
| Doom session | Exactly-one-gametic core contract, transactional input, ACK-independent frame staging, clean stop/restart | Fake-core host tests plus Cortex-M7 compilation |
| Doom video | Whole-frame 320-to-160 squish, fixed black background, deterministic best-three cell colors, immutable dirty records | Host conversion plus Cortex-M7 compilation |
| Current firmware baseline | V1.0.17/native25 builds; SHA-256 `20d0ac933ebb947cf0d5db13574e4fa329209cffcd283ac3cf1dc7d4444a1367` | Existing firmware only; Doom is not linked |

Pinned engine source:

- Repository: `https://github.com/Jean-MarcHarvengt/MCUME.git`
- Commit: `27f6b906aca34e06d6647bdca8215e25f8d20aa5`
- Subtree: `MCUME_teensy41/teensydoom`
- Engine notices: GPL-2.0-or-later on the Chocolate Doom-derived core files
- Distribution gate: 17 MCUME platform/glue files have no license grant in
  their headers or at repository level; see [the Doom licensing record](LICENSE.md)

The local test used the unmodified shareware v1.9 `DOOM1.WAD`:

- Bytes: 4,196,020
- MD5: `f0cefca49926d00903cf57551d901abe`
- SHA-256: `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`
- Policy: the full WAD is ignored test input and is never fetched, copied, or
  committed by these tools. The host proof writes an ignored final frame and
  a 768-byte RGB palette derived from the WAD's PLAYPAL lump.

The deterministic host run writes ignored evidence beneath
`build/doom/mcume-host-proof/`:

- Final indexed frame: 64,000 bytes, SHA-256
  `2b369e6ec80feb26c9c8c0e2a7346a86aebea78a0fa0bfe5130441665283b84b`
- PLAYPAL RGB palette: 768 bytes, SHA-256
  `fd895921b5d0a394612bb29852ed003d44d69f76dec31c0dc6b5d5fc7d63f7bb`

Converting that real frame produces exactly 1,000 unique 12-byte cell records:
12,000 payload bytes in 53 batches at the existing 19-record maximum. The
pixel-aspect-corrected PPM proof is 192,015 bytes with SHA-256
`c40b731993f724d7b3dadb7b24a735b09c5b1d152ccf73cdfb25b12bf3734664`.
It remains ignored under `build/doom/c64-frame-proof/`.

## Measured resource facts

The selected upstream core reserves an 8 MiB zone (`8,388,608` bytes). It also
needs a 64,000-byte indexed framebuffer. The MHS converter currently needs a
22,304-byte workspace and a 768-byte palette.

The unchanged V1.0.17/native25 firmware build recorded:

- 18,336 bytes of MinimalBoot stack reserve.
- 337,376 bytes of RAM2 heap reserve.
- 1,310,720 bytes of linker-reserved EXTRAM variables for the existing AGI
  view cache.

These figures are not additive free-memory promises. The Doom firmware port
must explicitly lease PSRAM while AGI/DOS are inactive, prove the board has at
least the required external memory, place the framebuffer/converter workspace
without violating the stack and RAM2 guards, and reset overwritten AGI cache
state before another engine starts.

The exact palette reducer preserved its output while reducing the representative
test from 14,560,000 to 460,194 candidate/sample evaluations. The real E1M1
frame required 568,935 evaluations. Cortex-M7 compilation reports a 480-byte
`renderCell` stack frame, guarded below 640 bytes. Host conversion took tens of
milliseconds, but host timing is not evidence of Teensy or end-to-end C64
frame rate.

The WAD is 18,100 bytes larger than the current 4,177,920-byte logical native
cartridge limit before any client, module, or metadata. Phase 1 therefore uses
separate WAD storage; a self-contained cartridge requires conversion or
compression later.

## Remaining integration gates

1. Add a GPL-preserving MCUME adaptation layer with an exactly-one-gametic
   entry point. Do not wrap its self-scheduling `D_DoomLoop` inside the MHS
   scheduler.
2. Replace MCUME's desktop/FatFs shim with bounded Teensy SdFat reads, expose
   the indexed framebuffer and current RGB palette directly, and lease the
   8 MiB PSRAM zone fail-closed.
3. Stage the pinned/adapted core through the firmware builder with source and
   patch provenance, then add a distinct Doom launch identity and reuse the
   stop-and-wait 12-byte cell transport.
4. Add the C64 launcher/presenter and atomic keyboard/port-2 input mailbox.
   A rejected make/break event must withhold ACK so releases cannot be lost.
5. Measure a continuously moving real Doom sequence on the physical C64 in
   PAL and NTSC where supported. Record frame-time distribution, packet count,
   input latency, PSRAM high water, stack high water, and failure recovery.
6. Add basic SID effects only after moving video and controls remain responsive.
7. Resolve or replace the 17 upstream platform/glue files with unclear license
   provenance before vendoring source or publishing a derived binary.

Phase 1 is complete only after the moving-view physical test and defensible
memory/timing report. Playable E1M1 remains the next separate acceptance gate.

## Commands

Fetch or verify the exact source without obtaining a WAD:

```powershell
.\doom\tools\fetch_mcume_teensydoom.ps1
.\doom\tools\test_mcume_source_lock.ps1
```

Run the engine proof with a separately supplied, untracked WAD:

```powershell
.\doom\tools\test_mcume_host.ps1 -WadPath <path-to-DOOM1.WAD>
```

Run the native platform slices:

```powershell
.\doom\tools\test_doom_runtime.ps1
.\doom\tools\test_doom_session.ps1
.\doom\tools\test_mpe_doom_video.ps1
```

Convert the real host frame into complete C64 cell records and a preview:

```powershell
.\doom\tools\render_doom_c64_proof.ps1 `
  -FramePath .\build\doom\mcume-host-proof\e1m1-final.indexed `
  -PalettePath .\build\doom\mcume-host-proof\playpal.rgb24
```
