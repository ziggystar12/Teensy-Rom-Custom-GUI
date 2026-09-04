# MHS Power Engine for TeensyROM+ firmware kit

> **Historical accelerator firmware guide.** This page preserves the earlier
> AGI+3 service and C64 fallback documentation. Its PowerVM, cartridge-layout,
> and VICE fallback statements do not describe native AGI cartridges. Use the
> [current firmware guide](../FIRMWARE-GUIDE.md) to install the current native
> release and [the root firmware builder](../../README.md#build-the-combined-firmware-on-windows)
> for the combined desktop/native engine. Legacy test results below retain
> their original scope.

This guide is copied into the generated firmware kit that accompanies an
AGI-64 cartridge built with the compiler's **MHS Power Engine for TeensyROM+** switch. That
generated kit contains experimental **MPE v1** firmware with the MHS AGI Engine
compatibility service for a TeensyROM+ Fab0.4
and a pinned official image for restoring stock firmware.

The same guide is also included as source documentation in the maintainer
handoff. The source handoff intentionally contains no `.hex` files: use its
rebuild script or the separate binary test package before following the flashing
steps below. Its `SHA256SUMS.txt` covers source, not firmware.

Firmware is not inside the `.crt`. The compiler does not flash the cartridge,
and the firmware contains no game. A Power Engine CRT remains a standard
EasyFlash or Magic Desk cartridge with all normal C64 code present.

## Compatibility

- Hardware: TeensyROM+ **Fab0.4** with a Teensy 4.1.
- Cartridge: EasyFlash 1 MiB, Magic Desk 16K 1 MiB, or Magic Desk 16K 2 MiB.
- Picture data: standard C64-RLE or the compiler's lossless Exomizer form.
- Emulator: VICE can run only the normal C64 fallback; it does not emulate this
  firmware service.

One installed Power Engine image works with every game. Game-specific
descriptors and optional bounded PowerVM bytecode remain in the CRT. MPE v1
capability discovery reports the AGI Engine and PowerVM services; it does not
load unrestricted native ARM modules. It also reports mailbox-only priority
line service `PQL` and the retained diagnostic `SCN` service.

The experimental image is named
`MHS-PowerEngine-TRPlus-v1_full.hex`. Do not substitute the older
protocol-v2 SQ1 test image for an AGI+3 CRT.

## What changes in a game

Matching firmware can:

- expand and DMA complete bitmap, screen, colour, and exact priority data;
- prepare the next indexed picture while the game continues after `load.pic`;
- prepare the current room's complete compiler-certified `GAC3` scene;
- retain diagnostic-only scatter-DMA for matching door, permanent-scenery, and selected animation patches;
- capture the completed live room after `show.pic` or `add.to.pic`;
- answer mailbox-only `$32 PQL` whole-footline checks from that token-bound
  room seed, with no C64 RAM access or `/DMA` assertion;
- expose a retained `$23` firmware capability that can cache `GBC1` cels and
  composite non-ego moving bitmap objects for instrumented qualification.

Production compiler output publishes `$10` full-picture DMA, `$21` scene
prefetch, `$22 ROOM_SEED`, and mailbox-only `$32 PQL`. A PQL request must return
the exact `$B2` tag/capability/token contract before the C64 uses its pass,
trigger, and all-water bits; every other outcome runs the unchanged native
priority scan. Production does not pack `MPS1` or dispatch DMA-backed `$31
SCN`, `$20 CELL_PATCH`, or `$23 ACTOR_FRAME`. The same generic firmware retains
those commands behind explicit diagnostic gates for Fab0.4 qualification.

The C64 uses one 19-byte bank-switch bridge at `$7FEC-$7FFE`. The bank-62
bitmap/compositor gate ends at `$7FC1`, and the UI/action gate ends at `$7FEB`,
so these paths no longer overwrite one another. The original C64 `GAC3` lookup,
complete safety preflight, patch writer, and compositor remain installed and
run whenever firmware is absent or declines a patch.

The first game-start loader remains visible. The Teensy+ build does not show
another loader bar for later room or introduction pictures. If the service is
absent, stock, incompatible, or reports a terminal error, the CRT runs the
complete C64 decoder and renderer behind a black display. A prefetch miss is
also safe; the game decodes synchronously or falls back. Builds made without
MHS Power Engine retain their normal visible loader behavior.

An accepted synchronous request is never abandoned while firmware could still
own DMA. A C64-side deadline posts reset `$7F`; busy firmware latches it as a
cooperative abort, releases its caches and bus ownership, makes cartridge bank
switching passable, and atomically publishes terminal `$EA`. The C64
acknowledges that terminal state and waits for ready `$00` before entering its
native fallback. A patch is fully validated and staged in Teensy RAM2 before
DMA begins. Each bounded bitmap/screen/colour segment performs and closes its
own DMA transaction, with no SD, cartridge, or scene-cache reads while `/DMA`
is asserted. That per-segment lifecycle is command `$20` only. The retained
firmware command `$23`, when enabled by an instrumented test build, stages the
complete actor frame first, then holds one scatter transaction from
its first bitmap write through all six bitmap, screen, colour, fallback,
descriptor, and terminal publication sites. It shares one `Started` state and
calls `AGIPictureCloseScatter` once; it never uses
`AGIPictureDMAWritePatchSegment`.

Priority acceleration is an exact data transfer, not an approximation.
Firmware rebuilds the same priority runtime form used by the C64. The patch
service accepts only complete, compiler-validated `GAC3` records and can retain
up to 65,535 bytes for one current-room scene. `ROOM_SEED` captures the live
8,000-byte bitmap at `$6000`, 1,000-byte screen at `$5C00`, 1,000 colour
nibbles at `$D800`, and the exact bounded format-2 or format-3 priority data at
`$8000`. The retained `ACTOR_FRAME` firmware handler reads the `GBC1` view index
and C64's 160-byte object table, caches the current cels, and composites non-ego
bitmap objects over that clean room when an instrumented qualification build
submits `$23`. Production compiler output does not. Object 0 (Roger) remains on
the C64's double-buffered VIC sprites by default. AGI logic, sound, input,
collision decisions, and sprite movement remain C64 work.

MinimalBoot linker-reserves a 1,310,700-byte no-load PSRAM arena: 20 independent
`$FFFF`-byte slots for every distinct current-room VIEW. Handler setup, IO2
activation, picture prefetch, and actor commands perform no runtime allocation
or arena-wide zero fill. Without enough PSRAM the service remains correct
through bounded cartridge reads and a 128-entry pattern cache, with potentially
higher first-frame latency.

The custom MinimalBoot image reserves at least 16 KiB for the Teensy launch and
SD stack. It does this by demand-paging two additional 8K cartridge blocks; no
firmware picture, patch, prefetch, room-seed, or actor-frame capability is
removed.

In an instrumented `$23` test, the moving-art service also synchronizes the
native live descriptors and clean-cell records after a successful frame.
Roger's priority/occlusion mask therefore sees the same non-ego actors, and a
later native fallback can erase or redraw them without leaving firmware-owned
pixels behind. A bad picture token, priority span, `GBC1` entry, object table,
or changed-cell bound fails closed before success is reported. In production,
the native compositor owns those records throughout.

Each accelerated picture carries a 16-byte `AGP3` descriptor immediately
before its visible stream. It identifies the C64-RLE/Exomizer combination,
EasyFlash or Magic Desk 2 layout, picture token, exact visible length, priority
source and format, and an XOR checksum. Firmware validates the descriptor,
source spans, exact output sizes, and token before publishing success.

| Request | Command | Success | Meaning |
| --- | ---: | ---: | --- |
| Full picture | `$10` | `$90` | decode and DMA visible plus exact priority data |
| Picture prefetch | `$11` | `$91` | prepare an indexed picture without blocking the C64 helper bank |
| Commit prefetch | `$12` | `$92` | DMA only the exact matching prepared picture |
| Cell patch | `$20` | `$A0` | retained diagnostic capability; compiler-disabled in production |
| Scene prefetch | `$21` | `$A1` | prepare one complete current-room `GAC3` scene without blocking the C64 helper bank |
| Room seed | `$22` | `$A2` | capture the complete live bitmap, screen, colour, and priority room planes |
| Actor frame | `$23` | `$A3` | retained firmware capability for instrumented qualification; compiler-disabled in production |

Prefetch changes latency only. A token, descriptor, or cached-span mismatch is
a miss and cannot commit stale data.

## Before flashing

In the generated binary firmware kit, keep these files together:

- `MHS-PowerEngine-TRPlus-v1_full.hex`
- `TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex`
- this guide
- `SHA256SUMS.txt`

Verify both `.hex` files against the complete values in that generated kit's
`SHA256SUMS.txt`. The checksums are generated from the exact build and are
intentionally not copied into this guide, so the guide cannot silently become
stale after a rebuild. Do not use the source-handoff checksum ledger for this.

Before installing, boot the official firmware and run
`TeensyROM Specific > TR+ C64 Expansion Port Test`. Record whether it passes;
that gives a useful DMA hardware baseline.

## Install the custom firmware

1. Power off the C64/128, attach TeensyROM+, insert the storage containing the
   kit, and power on.
2. In the TeensyROM menu, select
   `MHS-PowerEngine-TRPlus-v1_full.hex`.
3. Check the entire filename and press `Y` to confirm.
4. Keep the C64/128 powered throughout erase/programming. Do not reset, remove
   the cartridge, or interrupt power before the automatic reboot finishes.
5. Confirm the ordinary TeensyROM menu still opens.
6. Launch the CRT built with **MHS Power Engine for TeensyROM+**.

The custom dual image retains the pinned upstream upper/full firmware, normal
menu, and its network functions. In its lower MinimalBoot, **TCP Listen is
unavailable while any large-CRT session is running** because that memory is
reserved for deterministic paging and decode workspace. Networking is not
globally removed from the upper/full menu.

## What to test on real hardware

AGI+3 has passed source, build, conformance, and C64-fallback tests, but its
universal paths are not yet proven on a real board. Please record the machine's
video standard, CRT name, cartridge layout, and picture codec, then check:

1. The first game loader still appears.
2. Repeated full pictures appear correctly without later loader bars.
3. Walk-behind priority, collision, room boundaries, controls, music, and
   colour remain correct.
4. Compiler-certified doors and permanent background changes use Teensy patch
   DMA when matched, fall back to the C64 renderer when unmatched or declined,
   do not crash, and remain correct after actors move across the same cells.
5. Moving bitmap objects are correct through overlap, priority changes, room
   transitions, cel changes, and removal; no grey fringes or stale cells remain.
6. Roger stays smooth on his VIC sprites, passes behind the same scenery as the
   native build, and remains correct after a firmware frame declines to run.
7. Introduction and room animations neither lose required frames nor leave
   stale bitmap, screen, colour, or priority data.
8. Returning to the TeensyROM menu still works after several room transitions.

The desired coverage is:

| Area | Minimum real-hardware sample |
| --- | --- |
| Regression | SQ1, EasyFlash 1 MiB, C64-RLE, NTSC |
| Codec | one C64-RLE and one Exomizer build |
| Paging | Magic Desk 1 MiB and 2 MiB builds |
| Priority | full 13,440-byte and compact variable-length maps |
| Prefetch | hit, miss, and ordinary fallback |
| Patches | scene-prefetched and direct Teensy action DMA plus C64 `GAC3` fallback correctness |
| Room art | live-room seed after `show.pic` and `add.to.pic`; overlapping actor cels; native fallback handoff |
| Ego | object 0 retained as native VIC sprites with priority/occlusion parity |
| Timing | NTSC and PAL |
| Larger games | KQ4, Gold Rush, and SQ3 representative play |

If a picture is wrong, note whether it followed a `load.pic`/prefetch, whether
the prior room was correct, and whether the menu button still responds. If a
door or background object is wrong, note the room, action, actor positions,
and whether leaving and re-entering restores the scene.

## Restore official firmware

From a working TeensyROM menu, select
`TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex` and repeat the confirmed update
sequence above.

If the menu cannot boot, connect TeensyROM+ to a computer with a USB
A-to-micro-B cable while it remains installed in a powered C64/128. Open the
official restore `.hex` in PJRC Teensy Loader and press the white program button
on the Teensy module. Keep the C64/128 powered until programming completes and
the machine reboots.

## VICE fallback check

VICE emulates EasyFlash and Magic Desk but not the Teensy 4.1, the AGI+
challenge/mailbox, next-picture prefetch, or bus-master DMA. A Power Engine CRT
therefore keeps later pictures black while it uses its unchanged C64 decode and
drawing paths, including the native object compositor. The first game-start
loader still appears. Successful VICE play proves fail-closed compatibility
only; it does not prove any speedup.

## Source and reproducibility

The firmware build is pinned to TeensyROM commit
`3436b8fbd7c642ef9eabc691d3d09da08a6a6690` and applies, in order:

1. `0001-agi64-picture-dma.patch`
2. `0002-agi64-protocol-v3.patch`
3. `0003-agi64-actor-frame-scatter-settle.patch`
4. `0004-mhs-powerengine.patch`

The current patch `0003` is not the earlier
`0003-agi64-actor-frame-segmented-dma.patch` experiment. That rejected patch
released and reacquired `/DMA` between `$23`
publication sites by reusing command `$20`'s primitive. On a real NTSC Fab0.4,
that firmware redrew SQ1 Room 1 after the Astral Body request and then locked
before the robot animation, input, or menu could resume. The current four-patch
build retains `$20` per-segment DMA and `$23` single-held-scatter DMA, adds a
post-transfer settling interval, and layers MPE v1 over the compatible AGI
service.

From an AGI-64 source checkout, the reproducible entry point is:

```powershell
npm run build:teensyrom-plus:firmware
```

From the root of the extracted source handoff, the equivalent standalone entry
point is:

```powershell
.\scripts\build-teensyrom-plus-firmware.ps1
```

It runs the AGI picture conformance model, builds the Fab0.4 dual image with the
pinned Arduino/Teensy toolchain, copies the official restore image, and writes a
fresh build manifest plus `SHA256SUMS.txt`. It never flashes hardware.

The protocol-v2 SQ1 decode-only, reversible DMA probe, and visible-picture DMA
sequence did pass on a real NTSC Fab0.4. That evidence is useful for the DMA
foundation, but it is not evidence that the new AGI+3 layouts, codecs,
priority, picture-prefetch, action-DMA, or scene-prefetch paths have passed real
hardware yet. On August 30, 2026, the exact `55fc...` SQ1 reference cartridge
was run on a real NTSC Fab0.4 with firmware reported/assumed to be `2ded...`.
The parser display repainted/retyped on every key and showed a grey outline.
Dismissing the Astral Body message produced a visual Room 1 redraw and then a
hard lock. That redraw is not restored interpreter or input operation; robot
motion, tape retrieval, and resumed controls were not demonstrated. The frozen
9:04 PM artifacts preserve provenance but are not a hardware-pass rollback.
Reflash the exact checksummed firmware, power-cycle, and repeat the complete
route before classifying the pair.

The corrected `944c...` SQ1 trace completed Room 1 redraw and the retriever/tape
animation only after bypassing `$20`. Its flashing yellow `$23` marker reported
a terminal command failure; the native C64 compositor then completed the work.
Production therefore publishes `$10`, `$21`, and `$22` only. Both `$20` and
`$23` remain diagnostic-only, and their native C64 patch/object fallbacks stay
installed. A clean non-tracing build of that policy still requires physical
acceptance, as do live-room capture and both diagnostic publishers.
