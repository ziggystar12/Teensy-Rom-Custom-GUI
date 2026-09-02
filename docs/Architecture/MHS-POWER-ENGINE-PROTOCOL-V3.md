# AGI+3 universal accelerator for TeensyROM+ Fab0.4

> **Historical protocol-v3 handoff.** This describes the retained accelerator
> services and their original qualification state. Its patch instructions
> reproduce that handoff, rather than the current combined firmware. Use
> [the root firmware builder](../../README.md#build-the-combined-firmware-on-windows)
> and [Build Provenance](../BUILD-PROVENANCE.md) for the current combined firmware. Native AGI
> gameplay is documented in the [native firmware guide](../FIRMWARE-GUIDE.md)
> and implemented in `engine/native-game/`.

Source handoff for SensoriumEmbedded/TeensyROM<br>
Implementation: experimental AGI+ protocol v3, with protocol-v2 diagnostics retained<br>
Hardware status: v2 proven on NTSC Fab0.4; the SQ1 v3 retriever route and broader v3 matrix remain unproven

## Review package

This is a changed-code-only proposal against:

- Repository: `https://github.com/SensoriumEmbedded/TeensyROM`
- Base commit: `3436b8fbd7c642ef9eabc691d3d09da08a6a6690`
- Upstream version: firmware 0.8.0.4 beta
- Hardware target: TeensyROM+ Fab0.4 / Teensy 4.1

Apply both patches in order:

1. `patches/0001-agi64-picture-dma.patch`
2. `patches/0002-agi64-protocol-v3.patch`

`0001` is the protocol-v2 foundation: locked EasyFlash mailbox, C64-RLE picture
decode, NTSC/PAL selection, staged decode-only and reversible DMA diagnostics,
bounded bus-master DMA, and the MinimalBoot workspace policy. Its detailed
bring-up record remains in `AUTHOR-HANDOFF.md`.

`0002` is the v3 delta requested for upstream review: descriptor-driven picture
and priority decode, paged Magic Desk 2 support, Exomizer, exact `GAC3` cell
patches, picture/scene prefetch, live-room capture, `GBC1` cel caching,
moving-object composition, and a stronger challenge/response activation. It
contains no AGI game, CRT, compiled firmware, or compiler executable.

The firmware retains that complete command surface, including `$20 CELL_PATCH`
and `$23 ACTOR_FRAME`, for instrumented qualification. Production compiler
output publishes `$10` full-picture DMA, `$21` scene prefetch, and `$22
ROOM_SEED`, but does not dispatch `$20` or `$23`. Compiler-cached patches and
moving bitmap objects remain on their native C64 renderers until those paths
pass the real Fab0.4 matrix.

Do not apply the rejected
`0003-agi64-actor-frame-segmented-dma.patch`. Command `$20` correctly uses an
independent acquire/write/close transaction for every bounded patch segment,
but `$23 ACTOR_FRAME` has a different atomicity requirement. Patch `0002`
holds one scatter transaction across its six bitmap, screen, colour, fallback,
descriptor, and terminal publication sites. The rejected patch released and
reacquired `/DMA` between those sites and locked the real Fab0.4 SQ1 retriever
after the room redraw.

No compiled-firmware SHA-256 is embedded here. The reproducible build writes
the exact artifact, patch, and official-restore hashes into its manifest and
`SHA256SUMS.txt`; those generated values are the distribution authority.

## Why this design

AGI-64 already has a correct C64 interpreter and compiler. AGI+3 does not run
scripts, advance animation, choose cels, decide collisions, or replace the
game engine. It gives firmware three bounded jobs:

1. Expand compiler-described data into the exact bytes the existing C64
   runtime expects.
2. DMA only validated, absolute finished results into established C64 memory.
3. Cache compiler-packed cels and composite the C64-supplied current object
   state over an immutable clean-room seed.

The matching CRT always retains its complete C64 implementation. The
accelerator is discovered at runtime and is optional for correctness. With
stock firmware, a different cartridge, VICE, an invalid descriptor, a prefetch
miss, or a terminal service error, the cartridge runs the ordinary loader,
decoder, priority path, or cell renderer.

There are no game IDs, room IDs, title-specific checks, logic execution, or
game-state decisions in the firmware. The room-art path implements only the
bounded bitmap-cell composition contract shared with the C64 renderer.

## Changed firmware sections in patch 0002

| Source file | Changed-code responsibility |
| --- | --- |
| `Source/Teensy/MinimalBoot/Common/DMAControl_Minimal.h` | Adds bounded continuation/read support so bitmap, screen, priority, colour, object-state, descriptor, and patch segments use explicit bus-master ownership and retain emergency release ordering. |
| `Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_AGIPicture.c` | Replaces the narrow v2 handler with the backward-compatible v2/v3 service: challenge, capabilities, paged cartridge reader, bounded C64-RLE and Exomizer decode, `AGP3`, compact/full priority materialization, prefetch workspaces, `GAC3` scene validation, live-room seed, `GBC1` cel/object composition, scatter DMA, statuses, and errors. |
| `Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_EasyFlash.c` | Identifies bank 62 as the v2 helper and bank 59 as the v3 helper, blocks unsafe bank changes during synchronous ownership, and invalidates AGI workspace ownership before swap-buffer replacement. |
| `Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_MagicDesk2.c` | Tracks the active 16 KiB bank, exposes IO2 only for the v2/v3 helper bank or an active session, hooks polling, and protects and invalidates swap-buffer ownership without changing ordinary floating IO2 behavior. |
| `Source/Teensy/MinimalBoot/tests/agi-picture-conformance.mjs` | Locks the ABI, challenge, descriptors, codec bounds, exact priority layouts, EasyFlash/Magic Desk paging rules, room/actor mailbox layouts, bounded DMA calls, patch destinations, fallback metadata, prefetch ownership, and emergency-release structure. It is not a hardware DMA test. |

The Exomizer raw-forward decoder in `IOH_AGIPicture.c` carries the original
Magnus Lind attribution from the official decruncher source. Please review its
license/attribution placement as part of upstream acceptance.

## Matching C64-side sections

The C64 helper is reserved in cartridge bank 59 at raw `$EF800-$EFFFF`, mapped
to `$B800-$BFFF`. Fixed call points are:

- `$BF00`: synchronous command `$20` for one already-selected absolute patch.
- `$BF03`: nonblocking command `$21` for one indexed `GAC3` scene prefetch.

The collision-free `$7FEC` bank bridge routes `X=$37` through synchronous `$22
ROOM_SEED` before posting the retained `$21` scene prefetch. It also retains an
`X=$38` route to synchronous `$23 ACTOR_FRAME` for instrumented firmware work.
Production action-extension staging installs neither the `X=0` `$20` call nor
the `X=$38` visual-fallback call. Exact compiler-cached states use the native
C64 `GAC3` writer, while a lookup miss or unsafe selected patch jumps directly
to the unchanged native C64 compositor.

The matching host implementation and executable specifications are in:

- `host/teensyrom-plus-picture-runtime.mjs`
- `host/teensyrom-plus-action-runtime.mjs`
- `host/agi-action-cell-cache-v3.mjs`
- `host/agi-action-cell-cache-policy.mjs`
- `host/agi-teensy-room-actor-compositor.mjs`
- `test/teensyrom-plus-picture-runtime.test.mjs`
- `test/teensyrom-plus-action-runtime.test.mjs`
- `test/agi-action-cell-cache-policy.test.mjs`
- `test/agi-teensy-room-actor-compositor.test.mjs`

The helper supplies cartridge-layout identity, 24-bit raw offsets, lengths,
picture tokens, and KERNAL video-standard data. It never changes cartridge
banks while a synchronous command is accepted. Nonblocking prefetch commands
release the helper bank immediately by design.

## Activation and mailbox

Before activation, EasyFlash IO2 remains ordinary RAM and Magic Desk IO2 keeps
its upstream floating behavior. The v3 helper selects bank 59, writes challenge
`$3C` to `$DFFB`, then writes `A`, `G`, `I`, `+`, `$03` to `$DFF0-$DFF4`.
Unrelated locked IO2 access, a wrong byte, or a helper-bank change resets the
partial sequence.

Valid firmware responds with:

- `AGI+` at `$DFF0-$DFF3`
- protocol `$03` at `$DFF4`
- capabilities at `$DFF5`
- challenge response `$C3` at `$DFFB`

The response plus required capability bits is decisive; ordinary IO2 RAM can
no longer spoof support by echoing the C64's own signature writes.

| Address | v3 purpose |
| --- | --- |
| `$DFF0-$DFF3` | signature `AGI+` after activation |
| `$DFF4` | protocol version |
| `$DFF5` | capability bits |
| `$DFF6` | command write |
| `$DFF7` | status read |
| `$DFF8-$DFFA` | command-specific 24-bit raw source/index/cache offset, little endian |
| `$DFFB` | activation challenge/response before activation; command argument for `$22/$23`; error detail after terminal failure |
| `$DFFC-$DFFD` | command-specific length, address, or flags |
| `$DFFE` | machine flags; bit 0 is NTSC (`$02A6 == 0`) |
| `$DFFF` | picture/token byte |

Capability bits are `$01` full picture, `$02` prefetch, `$04` patch, `$08`
C64-RLE, `$10` Exomizer, `$20` compact priority, `$40` Magic Desk 2 paging, and
`$80` explicit timing. The current v3 implementation advertises all eight.

Protocol-v2 activation and commands `$01-$03` remain on helper bank 62 for the
existing SQ1 staged diagnostic CRTs.

## Commands and ownership

| Command | Request source | Success | Ownership and result |
| --- | --- | ---: | --- |
| `$10` full picture | raw `AGP3` descriptor; token in `$DFFF` | `$90` | synchronous decode plus one continuous DMA transaction |
| `$11` picture prefetch | raw 8-byte picture-index root; picture id in `$DFFF` | `$91` | posted/nonblocking decode; retains exact descriptor/token in workspace |
| `$12` commit prefetch | raw expected `AGP3` descriptor | `$92` | synchronous DMA only if the prepared descriptor matches exactly |
| `$20` cell-patch DMA | raw absolute patch; byte length in `$DFFC-$DFFD` | `$A0` | synchronous complete-record validation and scatter DMA |
| `$21` scene prefetch | raw `GAC3` root; picture id in `$DFFF` | `$A1` | posted/nonblocking copy of one complete scene, bounded to 65,535 bytes in up to eight 8 KiB slots |
| `$22` room seed | raw `GAC3` root; priority format/span; active picture in `$DFFF` | `$A2` | synchronously capture the complete clean live room from C64 video/priority memory |
| `$23` actor frame | raw `GBC1` view-index root; object-table base and native-object flag; active picture in `$DFFF` | `$A3` | retained firmware capability for instrumented qualification; synchronously composite non-ego moving bitmap objects and commit bounded changed cells plus fallback metadata |

Status `$00` is ready, `$01` is decoding, and `$02` is DMA. `$E0-$FF` are
terminal errors. `$00` acknowledges a result. `$7F` resets and relocks an idle
session; while `$01/$02` is visible it instead latches a cooperative abort.

The abort runs outside the bus ISR. It releases source, picture, and scene
ownership, then uses one short interrupt-masked publication window to clear the
helper-bank interlock before making terminal `$EA` visible. Acknowledgement also
clears that interlock before publishing ready `$00`. The C64 waits for this
`$7F -> $EA -> $00 acknowledgement -> ready $00` sequence before native
fallback, so fallback cartridge writes cannot race firmware decode or DMA.
Cell-patch DMA first validates and materializes the complete patch (currently
bounded to 13,000 encoded bytes) in Teensy RAM2, then releases every cartridge,
scene-cache, or SD source. Each bounded bitmap/screen/colour segment calls the
existing DMA acquire/write/close path independently. No source paging occurs
while `/DMA` is asserted, and no later segment starts before the prior close has
returned the bus drivers to input.

That independent lifecycle is specific to command `$20`. Command `$23` first
stages the complete actor-frame delta and fallback metadata, then creates one
`Started` scatter transaction. All six publication call sites use
`AGIPictureDMAWriteSegment`; one `AGIPictureCloseScatter` follows the terminal
`$5B10-$5B12` bytes. `$23` must not call
`AGIPictureDMAWritePatchSegment`, `PerformDMA`, or `CloseDMA` inside the
publisher because the C64 cannot safely resume between a half-published actor
frame's pixels and compositor metadata.

Commands `$11` and `$21` do not hold bank 59 while their work is pending. A
later commit/patch must still match the cached raw span and token. Synchronous
commands, including `$22` and `$23`, retain the helper-bank lock until a
terminal status because the C64 must not switch the cartridge source while
firmware may still decode or own the bus.

The production C64 action bridge occupies only `$7FEC-$7FFE`, after the
bitmap/compositor and UI gates. It selects helper bank 59: `X=$37` captures the
live room with `$22` before posting `$21` scene prefetch. The same generic
bridge retains `X=0` for `$20` and `X=$38` for `$23`, but production has no call
site for either selector. An instrumented build can opt into each path for
qualification. The bridge restores action bank 61 and returns carry clear when
firmware is unavailable or declines a request. Production `GAC3` states and
moving objects enter their normal C64 renderers directly. If `$22` is
unsupported, selector `$37` still posts `$21`.

## AGP3 picture descriptor

Sixteen bytes immediately precede the visible stream:

| Offset | Size | Contract |
| ---: | ---: | --- |
| `0` | 4 | ASCII `AGP3` |
| `4` | 1 | descriptor bytes, exactly `$10` |
| `5` | 1 | visible codec: `0` RLE all; `1` Exomizer bitmap plus RLE screen/colour; `2` Exomizer all |
| `6` | 1 | bit 0 Magic Desk 2 layout; bit 1 priority present; all other bits zero |
| `7` | 1 | expected picture token |
| `8` | 2 | total visible encoded bytes, little endian |
| `10` | 3 | raw priority source, little endian |
| `13` | 2 | low 14 bits encoded priority length; bit 15 compact format 3; bit 14 Exomizer format 4; neither means C64-RLE |
| `15` | 1 | XOR checksum of bytes `0..14` |

The layout flag must agree with the active handler. Source spans are checked
against the complete CRT payload, including Magic Desk 2 1 MiB/2 MiB images and
tagged pages. The stream must be consumed exactly; trailing or short encoded
data is an error.

Visible output is fixed:

| Segment | Expanded bytes | C64 destination |
| --- | ---: | ---: |
| bitmap | 8,000 | `$6000` |
| screen | 1,000 | `$5C00` |
| colour | 1,000 | `$D800` |

Full priority C64-RLE or Exomizer output is exactly 13,440 bytes at `$8000`.
Compact priority format 3 starts with its `$A3`, 160-column, 168-row header and
declared runtime length. Firmware rebuilds the exact mutable row-pointer and
run-record representation at `$8000`; its length is variable and must be no
greater than `$3300`. It is not expanded to a guessed 8,000-byte pixel plane.

DMA segment order is bitmap, screen, priority when present, then colour. The
new continuation helper keeps those transfers under a single bus seizure and
calls the existing bounded close/release path once.

## GAC3 exact cell patches

`GAC3` is a compiler-produced sparse cache of complete scene states. Firmware
does not interpret AGI objects or draw commands while applying a `GAC3` patch;
it transfers the compiler's finished absolute cells.

The 18-byte cache header contains `GAC3`, version 3, scene count, 26-byte state
entry size, flags, directory/data offsets, 24-bit total length, 8-byte object
key size, 10-byte cell record size, and 7-byte scene-directory entry size. Each
sorted 7-byte scene entry contains picture id, a 3-byte relevant-object filter,
and a 24-bit local scene offset.

Command `$21` validates this header and directory, finds the requested picture,
derives its exact end from the next sorted entry or total length, and copies up
to 65,535 bytes into as many as eight 8 KiB scene-workspace slots. A later `$20`
may read a patch directly from the cartridge or from that exact prepared span.

An absolute patch is a sequence of:

```text
cellLo, cellHi, count,
count * (bitmapByte0..bitmapByte7, screenByte, colourByte)
```

Cells are `0..999`; a run has `1..40` cells and cannot cross a 40-cell row.
Firmware validates the entire supplied length, every run, and every target
before starting DMA. It then writes each cell to:

- bitmap: `$6000 + cell * 8`, eight bytes
- screen: `$5C00 + cell`, one byte
- colour: `$D800 + cell`, low nibble

Priority data is deliberately absent from cell patches. The patch is an
absolute, idempotent final visible state. Any unsupported or incomplete state
continues through the unchanged C64 renderer.

The compiler admits caches only as complete scenes. Its capacity policy pays
the exact `GAC3` header/directory/scene cost, honors pinned scenes, maximizes
estimated C64 cycles saved, preserves a configured primary-free-space floor,
and never publishes a partial room state set.

## ROOM_SEED and ACTOR_FRAME protocol contract

Command `$22 ROOM_SEED` establishes the immutable clean-room base used by the
moving-object compositor. Its mailbox is:

| Address | `$22` value |
| --- | --- |
| `$DFF8-$DFFA` | current `GAC3` root, raw 24-bit little endian; retained for the following `$21` scene prefetch |
| `$DFFB` | priority runtime format, exactly `2` or `3` |
| `$DFFC-$DFFD` | exact/bounded priority bytes: `$3480` for format 2, no more than `$3300` for format 3 |
| `$DFFE` | timing flags only; bit 0 is NTSC |
| `$DFFF` | active picture token |

After validating the token, format, and span, firmware DMA-reads the completed
live room from C64 memory:

| Plane | C64 source | Bytes |
| --- | ---: | ---: |
| Bitmap | `$6000` | 8,000 |
| Screen | `$5C00` | 1,000 |
| Colour RAM | `$D800` | 1,000; low nibble retained |
| Priority runtime | `$8000` | `$3480` for format 2 or the validated bounded format-3 span |

The seed is posted after `show.pic` and repeated after a permanent
`add.to.pic`, so it includes every completed C64-side background change. Any
fallible read invalidates the prior seed before `$A2` can be reported.

The compiler keeps `$20` and `$23` disabled in production. An instrumented
qualification build can submit the retained actor-frame command, whose mailbox
is:

| Address | `$23` value |
| --- | --- |
| `$DFF8-$DFFA` | cartridge raw 24-bit `GBC1` view-index root |
| `$DFFB-$DFFC` | C64 address of the first object-state array (`object_x`), little endian |
| `$DFFD` | native-first-object flag; `1` means object 0 stays on VIC sprites and is excluded from bitmap composition |
| `$DFFE` | timing flags only; bit 0 is NTSC |
| `$DFFF` | active picture token, which must match the current seed |

The C64 object table is 160 contiguous bytes: eight arrays of 20 objects in
this order: x, y, view, loop, cel, priority, direction, and screen flags.
Firmware validates every referenced `GBC1` view/loop/cel, caches the required
cel artwork, and composites visible bitmap objects in stable AGI
priority/object-id order over the immutable clean-room seed. The dirty set is
the union of pending `$20` cells and the clipped rectangular bounds of every
changed actor's old and new cels; it is fully materialized and bounded to the
C64 compositor's 212-cell capacity before the first write.

MinimalBoot linker-reserves a no-load PSRAM arena of 1,310,700 bytes: exactly 20
independent `$FFFF`-byte VIEW slots. The actor service separately materializes
the 256-entry/768-byte `GBC1` root. If enough PSRAM is unavailable, the same
decoder uses bounded cartridge reads and a 128-entry pattern cache. No picture,
prefetch, actor, activation, or idle-reset path performs runtime allocation or
an arena-wide zero fill; activation publishes bounded state and reset posts
cleanup to the main poller.

The AGI build reserves 16 KiB of DTCM by reducing `MaxRAM_ImageSize` by two 8K
resident blocks. Those blocks remain demand-pageable, so no accelerator feature
or cartridge capacity is removed. Packaging fails when `_estack - _ebss` is
below 16 KiB.

A successful instrumented `$23` writes only changed bitmap/screen/colour cells,
then keeps the C64 fallback state coherent. It publishes 20 live descriptors at
these
fixed arrays: x `$4ED5`, y `$4EE9`, priority `$4EFD`, width `$4F11`, height
`$4F25`, flags `$4F39`, raw cel bytes `$4F4D/$4F61/$4F75`. The prior
clean-cell records start at `$B480`, use 12 bytes per cell, and are counted at
`$5B10`.
This metadata is part of the commit, not an optional follow-up: object 0's
native priority mask consumes the descriptors, and a later C64 compositor
fallback consumes the clean records. If firmware cannot publish a complete
coherent frame, it must return a terminal error instead of `$A3`.

The publication is one held scatter transaction. A shared `Started` value is
passed to `AGIPictureDMAWriteSegment` at the bitmap, screen, colour, prior
clean-cell, descriptor, and transaction-tail sites; the publisher performs one
`AGIPictureCloseScatter` after the final site. This is intentionally different
from `$20`'s bounded per-segment acquire/write/close lifecycle.

`$20` direct `GAC3` patches and `$21` scene prefetch remain valid services.
While a dynamic seed is active, a successful `$20` patch must make its affected
cells visible to the next actor-frame dirty union so a cached actor frame cannot
restore stale background bytes.

Cold and warm frames use the same live-room compositor. Any resolution,
staging, or DMA failure publishes no success status and invalidates retained
room art until the next explicit `$22` seed. The C64 can then run its unchanged
fallback without firmware later adopting an unverified framebuffer.

## Picture and scene prefetch contract

`load.pic` posts `$11` with the raw picture-index root and picture id. Firmware
resolves the indexed descriptor and decodes all visible and priority output
without blocking the C64 helper bank. At `draw.pic`, `$12` commits only the
identical prepared descriptor. A miss or invalidation reports an error so the
caller can use `$10` or its C64 decoder.

After `show.pic`, the C64 can post `$21` for that room's complete `GAC3` scene.
Later patch lookup/application remains keyed by the exact compiler-proven
object state. Prefetch changes latency only; it cannot change which picture or
patch is legal.

## Paging and workspace ownership

AGI+3 requires at least fourteen 8 KiB MinimalBoot swap buffers: eight can
retain one prepared scene, three hold decoded picture and priority segments,
one resolves a paged source, and two remain mapped as cartridge LO/HI slots.
The source reader supports:

- directly resident CRT chip pointers;
- already-loaded tagged swap-cache pages;
- bounded SD reads for a tagged page not currently resident.

EasyFlash and Magic Desk polling call the AGI handler before overwriting a swap
slot. Any decoded picture, source page, or prefetched scene owning that slot is
invalidated or released first. The handler rejects spans outside the declared
CRT image and never treats a tag as a direct pointer.

## DMA and fallback safety

The v2 NTSC/PAL fix remains: the C64 derives video standard from KERNAL `$02A6`
and v3 carries it in `$DFFE`. Firmware selects the pinned upstream timing before
any DMA-capable command.

DMA keeps the v2 independent deadlines:

- 250 ms for main-context DMA state transitions
- 100 microseconds for every PHI2-edge wait in the ISR

On expiry, R/W, address, and data drivers return to input before `/DMA` is
released, then terminal status `$EA` is published. The C64 may take its exact
fallback only after terminal completion/error. Before command acceptance it is
free to fall back immediately.

The first game-start loader remains. Later accelerated pictures and their exact
C64 fallback stay behind a black display. The stage-28 loader change is
deliberately narrow: it removes the redundant loader-surface restoration
between the authored `Welcome` text and the following title picture. It does
not remove or alter the initial loader, and non-Teensy builds retain the
ordinary visible loader path.

## Memory and network tradeoff

The custom dual image does not remove the pinned upstream upper/full firmware,
its menu, or its network code. MinimalBoot TCP Listen is excluded while **any
large-CRT session** is running under this image, not only while an AGI command
is active. This makes the fourteen-slot workspace and decode/prefetch ownership
deterministic. TCP Listen remains unavailable for the duration of that launched
session; returning to the upper/full menu restores the unchanged upper
firmware behavior.

Upstream may choose another workspace policy. If TCP Listen is restored, the
fourteen-buffer ownership, paging, and invalidation contract must still be proven
under concurrent network use.

## Apply, test, and build

This repository already contains the applied source. To replay the ordered
patches against another clean TeensyROM checkout:

```powershell
$teensyRomSource = 'C:\path\to\TeensyROM'
$patchRoot = (Resolve-Path .\patches).Path
git -C $teensyRomSource checkout --detach 3436b8fbd7c642ef9eabc691d3d09da08a6a6690
git -C $teensyRomSource apply --check (Join-Path $patchRoot '0001-agi64-picture-dma.patch')
git -C $teensyRomSource apply --whitespace=nowarn (Join-Path $patchRoot '0001-agi64-picture-dma.patch')
git -C $teensyRomSource apply --check (Join-Path $patchRoot '0002-agi64-protocol-v3.patch')
git -C $teensyRomSource apply --whitespace=nowarn (Join-Path $patchRoot '0002-agi64-protocol-v3.patch')
git -C $teensyRomSource diff --check
node (Join-Path $teensyRomSource 'Source\Teensy\MinimalBoot\tests\agi-picture-conformance.mjs')
```

Then use the upstream build entry point:

```powershell
& (Join-Path $teensyRomSource 'Source\Teensy\tools\Build-DualBoot.ps1') `
  -Fab04_Features
```

The complete AGI-64 repository provides this pinned end-to-end entry point:

```powershell
npm run build:teensyrom-plus:firmware
```

The extracted source handoff provides the same wrapper directly, without an
`npm` project:

```powershell
.\scripts\build-teensyrom-plus-firmware.ps1
```

That wrapper installs Arduino CLI 1.4.1, Teensyduino 1.61.0, and CRC32 2.0.0,
applies the patch series, runs conformance, builds without flashing, preserves
the official restore image, and emits a manifest plus fresh SHA-256 checksums.

From the complete AGI-64 repository root, matching C64-side tests are:

```powershell
node --test test/teensyrom-plus-picture-runtime.test.mjs `
  test/teensyrom-plus-action-runtime.test.mjs `
  test/agi-action-cell-cache-policy.test.mjs
```

VICE should also run representative EasyFlash and Magic Desk AGI+3 CRTs through
their no-service C64 fallback. VICE cannot exercise the firmware path.

## Upstream review contract

Please evaluate the proposal in distinct gates. Passing an earlier gate does
not imply the later one.

### 1. Source and ABI review

- Confirm ordinary EasyFlash IO2 RAM and Magic Desk floating IO2 are unchanged
  outside helper banks or an active session.
- Confirm the `$3C`/`$C3` challenge prevents signature echo false positives.
- Review all raw-offset arithmetic, page tags, SD reads, swap ownership, stream
  bounds, integer widths, and exact-consumption checks.
- Compare the Exomizer decoder with the attributed upstream raw-forward source.
- Confirm v2 commands and the staged SQ1 diagnostics remain compatible.

### 2. Build and conformance

- Both patches apply cleanly to the pinned commit.
- `git diff --check` is clean.
- The host conformance model passes.
- Fab0.4 dual firmware compiles with the pinned upstream toolchain.
- The upper/full firmware and official recovery image remain usable.

### 3. Safe fallback

- Stock firmware and VICE reject AGI+3 without a hang or wrong-bank read.
- EasyFlash 1 MiB and Magic Desk 1/2 MiB CRTs remain playable via the complete
  C64 loaders/decoders.
- A malformed descriptor, codec stream, compact priority map, `GAC3` scene, or
  patch returns a terminal error before unsafe DMA.
- A prefetch miss cannot commit stale picture or scene data.
- A stale room token, invalid live priority span, malformed `GBC1` cel, object
  table error, or changed-cell overflow cannot publish `$A2/$A3` or leave a
  partial actor frame.
- A successful instrumented actor frame leaves descriptors and clean-cell
  records usable by Roger's native occlusion mask and by the next C64 fallback
  frame. Production misses never transfer ownership away from that native
  compositor.

### 4. Current Fab0.4 evidence

The frozen 9:04 PM SQ1 CRT/firmware artifacts preserve a previously reported
result; they are not a current hardware-pass rollback. On August 30, 2026, a
physical NTSC Fab0.4 run of the exact `55fc...` cartridge with firmware
reported/assumed to be `2ded...` repainted/retyped the parser display on every
key and showed a grey outline. Dismissing the Astral Body message produced a
visual Room 1 redraw and then a hard lock. A redraw is not restored interpreter
or input operation: that run did not demonstrate robot motion, tape retrieval,
or resumed controls.

The installed firmware identity was reported/assumed rather than independently
reconfirmed during that run. The exact checksummed `2ded...` image must be
reflashed, followed by a power cycle and the complete retriever/input test,
before the pair or route can be classified. This observation does not by itself
assign the lock to `$23`.

The corrected `944c...` trace completed Room 1 redraw and the retriever/tape
animation after bypassing `$20`. Its flashing yellow `$23` marker reported a
terminal command failure; the native C64 compositor then completed the work.
Production therefore publishes `$10`, `$21`, and `$22` only. Both `$20` and
`$23` call sites stay behind explicit diagnostic gates. The rejected
segmented-DMA patch remains excluded, and the firmware's retained `$23`
publisher continues to use the single-scatter atomicity contract documented
above.

The remaining universal v3 matrix is **pending** until real hardware covers:

- EasyFlash 1 MiB and Magic Desk 16K 1 MiB/2 MiB;
- C64-RLE and Exomizer visible streams;
- full RLE/Exomizer and compact priority materialization;
- picture prefetch hit/miss and synchronous picture decode;
- direct and scene-prefetched `GAC3` door/background/animation patches;
- room seed after `show.pic` and `add.to.pic`, for priority formats 2 and 3;
- instrumented `$23` resting frames without grey fringes;
- overlapping/non-overlapping moving-object frames, cel and priority changes,
  disappearance, room transition, and forced native fallback;
- object 0 retained as double-buffered VIC sprites with correct walk-behind
  masking against firmware-composited non-ego actors;
- NTSC and PAL timing;
- collision, walk-behind priority, colour, audio, controls, menu return, and
  repeated transitions in representative SQ1, KQ4, Gold Rush, and SQ3 play.

### 5. Product-policy decision

- Decide whether the feature remains opt-in or becomes an official capability.
- Decide whether MinimalBoot TCP Listen exclusion for all large-CRT sessions is
  acceptable.
- Decide whether the Exomizer attribution and duplicated MinimalBoot DMA helper
  should be reorganized for upstream maintenance.
- Keep the official restore route and clearly label experimental builds until
  the hardware matrix passes.
- Keep `$20` and `$23` compiler-disabled until their instrumented Fab0.4
  matrices pass; `$10`, `$21`, and `$22` are the production AGI Engine
  services.

The earlier v2 SQ1 results prove the underlying NTSC decode/DMA route on one
real Fab0.4. The 9:04 PM artifacts preserve provenance but currently prove no
v3 retriever pass. The latest MPE v1 `$23` path is not accepted, and the
segmented `$23` experiment is specifically disproven and excluded. The results
do not yet prove Magic Desk paging, Exomizer, priority, the full prefetch/patch
matrix, production containment, PAL, or other games. This handoff should
therefore be reviewed as an experimental upstream option, not as a replacement
for the official image.
