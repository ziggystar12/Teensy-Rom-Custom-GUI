# MPE firmware video service

Status: NESVM now opts into the firmware-owned indexed service. The existing
fast DMA host, cooperative NES scheduling and repaired picker remain included.
DOS/DOOM integration is later work; AGIVM does not opt in and is unchanged.
The earlier reference converter and policy are retained as foundations; live
conversion uses the bounded `mpe_video_live` companion in firmware.

The neutral ABI accepts native dimensions/stride, 8-bit indices and an RGB
palette. NES submits 256x240 and firmware resolves the selected mode. Default,
Auto-8 and Enhanced-25 scale to the whole 320x200 canvas. Sharp centers sources
narrower than 320 at native column width: NES uses columns 32-287 with 32 black
columns on either side, avoiding uneven 256-to-320 horizontal stretching.
Vertical fitting remains 240-to-200; sources at least 320 wide still fit to 320.
This is firmware policy based on source geometry, not a game or VM-name check.
It requires 24 KiB of module-lent RAM1
workspace plus the producer's immutable source frame. Current live conversion
uses deterministic dominant-color pairs, not an exhaustive per-frame search.

Default for NES is ordinary 160x200 multicolor; Sharp is 320x200 with two colors
per 8x8 cell. Enhanced modes add one shared split position within each selected
8-line band, allowing separate upper/lower color pairs. This is NOT unrestricted
four independently placeable colors per 8x8 cell. Auto-8 selects at most eight
beneficial bands; Enhanced-25 permits all 25. Plan hysteresis reduces churn.

Steady Default/Sharp frames retain one held-DMA transfer plus SID/frame-end.
Timed modes and mode changes use PAUSE -> DMA -> RESUME -> SID/frame-end: the
C64 raster kernel cannot keep its cycle alignment while DMA owns its CPU bus.
The screen is briefly hidden during transfer; enhanced modes can therefore
cost cadence or flicker. Reduced two-screen FLI also has the VIC's leftmost
24-pixel rescan artifact. Physical PAL/NTSC appearance, input latency and speed
remain acceptance gates, not claims established by the software tests.

Verification covers native module/picker/immutable frames, real host service
validation and mocked DMA lifecycle, actual emitted selector/receiver code,
and VICE PAL/NTSC raster timing for three frames with full and mixed plans.
Native converter regression checks cover every output pixel of centered Sharp,
black padding, source stride, unchanged vertical fitting, and full-width output
after switching back to the other modes. Centered Sharp is a source change
after V1.1.3; the published V1.1.3 firmware does not include it.
Mixed plans exercise all seven split positions, including row-7 YSCROLL reset.

## Platform rule

The MHS Power Engine is the hardware platform. It owns C64 input capture,
video presentation, sound publication, cartridge transport and frame timing.
A VM owns its emulated machine or application, but it must not need to know how
the VIC-II, SID, CIA keyboard matrix or cartridge mailbox implements those
services.

The target relationship is:

```text
VM or application
  -> neutral video frames, logical input events and logical audio
MPE firmware services
  -> scaling, VIC palette reduction, presentation policy and packet freezing
C64 client
  -> VIC-II raster/bitmap publication, CIA sampling and SID register writes
```

This permits NESVM, DOSVM, a future Amiga VM, a video player or another engine
to use the same output service. No firmware branch may select behavior by an
engine name or by an individual game.

## Earlier NES transport repair (V1.1.1 baseline)

The September 4, 2026 performance candidate adds a host-owned synchronous
`VIC_CELL10` transport as an intermediate step toward the neutral service.
NESVM still converts its PPU output to the established 40x25x10-byte VIC frame,
but asks MPE firmware to present that immutable frame. On Fab 0.4 hardware the
host scatters its bitmap, screen and color planes to `$6000`, `$5c00` and
`$d800` through the existing held bus-master DMA implementation. There is no
game ID, ROM hash or content-specific decision in this path.

This migration call reports `Transferred`, not `Presented`: it accepts only a
zero background and an already-established, unchanged hires/multicolor mode.
The ordinary SID/frame-end packet still commits display policy and audio. The
future indexed service remains responsible for owning arbitrary backgrounds,
mode selection and atomic frame presentation.

This targets a measured bottleneck. A scrolling 1,000-cell Sharp frame needed
53 acknowledged CELL packets plus its frame-end packet. The actual generated
C64 receiver executed 281,399 instructions for a synthetic replacement, which
alone imposes a best-case floor above 562,798 6510 cycles before emulator cost.
The DMA path replaces those repeated packet copies with one firmware-owned
frame transfer and the normal SID/frame-end acknowledgement.

The first image and any mode replacement still use the packet receiver to
establish its display and color-shadow state. A busy DMA service leaves the
frozen frame untouched for retry; an unavailable or failed service uses the
old CELL path. Old clients omit the PAL/NTSC timing marker and therefore fail
safely to that fallback. Host tests cover all three results, PAL and NTSC
clients publish distinct timing markers, and the matched firmware links within
its fixed memory regions. NESVM deliberately requires the VIDEO service in this
matched TeensyROM+ Fab 0.4 candidate; the runtime fallback is not support for
older firmware or non-DMA hardware. Physical SMB playability, bus timing and
sustained frame cadence remain mandatory acceptance gates.

That pre-indexed ABI 2 implementation was the migration source. The indexed
tail extension now moves NES gameplay conversion and policy into firmware;
other VMs retain their established services until explicitly migrated.

DOSVM shows why guest mode and presentation policy must be separated. Its
current Tandy mode 09h path is always rendered as 320-wide hires and bypasses
the Sharp preference, so the existing F7 shortcut can change a stored flag
without changing the picture. Under this service, Tandy is simply another
indexed source: Default, Auto-8, Enhanced-25 and Sharp resolve in firmware just
as they do for any other VM.

## User presentation controls

Only the exact unshifted `Ctrl+Commodore+Fn` chord is a platform selector.
Ordinary function keys, `Ctrl+Fn`, `Commodore+Fn` and shifted F2/F4/F6/F8
remain VM input.

| Chord | Direct selection | Purpose |
| --- | --- | --- |
| `Ctrl+Commodore+F1` | Default | Restore the selected VM package's declared default. |
| `Ctrl+Commodore+F3` | Auto-8 | Use reduced two-screen FLI only in the eight most valuable 8-line bands. |
| `Ctrl+Commodore+F5` | Enhanced-25 | Use the same technique in every band where it improves the image. |
| `Ctrl+Commodore+F7` | Sharp | Force established 320x200, two-color-per-8x8 hires output. |

These are selections, never toggles, so the displayed policy cannot drift from
the key the user pressed. F1 is not synonymous with the color renderer: it
means "return to this VM's configured preference." A VM package declares a
default and a supported-mode mask. For example, a high-color/high-resolution
VM can default to Auto-8 while AGIVM can declare Default-only and retain its
existing output and all authored Commodore-function-key actions.

The physical C64 matrix has one unavoidable ambiguity. F3 and Commodore share
a matrix column, so `Ctrl+Commodore+F3` produces the same sensed rectangle as
`Ctrl+Commodore+Cursor Right`. A selector-enabled client will interpret that
pattern as Auto-8 and consume both the real and phantom key until both release.
This deliberate tradeoff requires a physical keyboard test. It cannot be
resolved in software after the matrix is read.

## Generic video input

The live implementation accepts native-sized indexed frames plus an RGB
palette, explicit width/height/stride and a frozen generation. The reference
converter accepts 320x200. Further service extensions should describe:

- width, height, stride and pixel format;
- an RGB palette for indexed sources;
- pixel aspect and whole-frame/crop policy;
- a monotonically increasing frame identifier;
- a begin/scanline/end submission contract; and
- whether the VM permits frame dropping while the previous image is frozen.

Scanline submission avoids requiring a large universal framebuffer in either
the firmware or every VM. The firmware samples source rows monotonically,
scales to the 320x200 VIC canvas, maps colors and owns the converted target
before returning from `video_end`. A busy result lets an emulator keep its
clock authoritative and offer a newer frame later instead of blocking guest
time on C64 transfer speed.

The VM never submits a presentation mode with each frame. The firmware resolves
the package default and any platform hotkey override. Mode changes take effect
only at a complete frame boundary.

## Firmware presentation profiles

### Default

Default resolves to the package preference. A package can choose the existing
160x200 multicolor reducer, Sharp, or Auto-8. A compatibility-only native-cell
source may help migration, but it is not the final VM contract.

### Color

Color preserves the established 160x200 multicolor-bitmap path: one shared
background plus three cell-local colors. The portable converter accepts a
firmware-selected VIC background and carries it in the frozen frame plan; the
service must choose that background automatically from neutral source content
and policy rather than asking a VM or game profile.

### Sharp

Sharp preserves 320 horizontal positions and selects one deterministic color
pair for every 8x8 cell. It uses the existing bitmap and primary screen map and
is the safe high-resolution fallback.

### Auto-8

For each 8-line band, the firmware compares Sharp with one additional vertical
split. Each side of the split receives its own two-color pair. It scores the
total perceptual error reduction across all 40 cells, applies a transport/raster
penalty and selects at most eight bands.

Stable tie-breaking, a minimum improvement threshold, preference for bands
used by the preceding frame and a minimum hold period prevent the raster plan
from chasing moving content. There are no game profiles.

The portable foundation currently implements deterministic positive-gain
ranking and the eight-band cap. The threshold, preceding-frame preference and
hold period are service-integration work and must land before Auto-8 is enabled
as a package default.

### Enhanced-25

Enhanced-25 applies the same two-pair method to every beneficial band. It is a
quality ceiling and raster stress mode, not the recommended default. The SMB
reference frame showed only a small improvement over Auto-8, but keeping this
mode provides a direct physical comparison without adopting full FLI.

Full row-by-row FLI is intentionally excluded. It needs eight screen maps,
substantially larger frame data and far more forced badlines, while conflicting
with existing client memory users. It is not a sensible general real-time VM
profile.

## C64 representation

The existing 12-byte indexed cell record can remain intact:

```text
cell index (2) + bitmap rows (8) + attribute A (1) + attribute B/color (1)
```

Color and Sharp retain their established interpretation. In Auto-8 and
Enhanced-25, hires color RAM is unused, so the final byte becomes the attribute
for a second 1 KiB screen map. The intended VIC bank layout is:

| Address | Use |
| --- | --- |
| `$6000-$7fff` | Shared 8 KiB bitmap |
| `$5c00-$5fff` | Primary screen attributes (`$D018=$78`) |
| `$5800-$5bff` | Secondary screen attributes (`$D018=$68`) |

A versioned video-state packet precedes a complete replacement and contains
the resolved profile, background, 25-bit enhanced-band mask and split position
for each selected band. That state, its cells and its raster schedule remain
immutable until the frame is acknowledged.

The portable record producer follows the same lifetime rule: an offered cell
batch does not update its shown state or published plan until acknowledged. A
rejected batch remains dirty and is reproduced identically for retry.

The C64 raster client switches screen maps and forces the required mid-cell
fetch only for enhanced bands. Any mode or band-plan change uses the existing
hide, complete replacement and border-aligned reveal rule. A partial frame may
never be interpreted under a new profile.

## Service and package boundary

The next VM ABI adds an append-only, versioned video service owned by the host.
Its responsibilities are:

- validate and accept neutral frame submissions;
- own source-to-VIC scaling and palette conversion;
- hold target, shown and dirty-cell state;
- resolve package default, capabilities and user override;
- score/freeze Auto-8 and Enhanced-25 raster plans;
- produce immutable cell and video-state packets; and
- expose backpressure and counters without blocking guest execution.

The module no longer emits video packets. Its remaining packet path is removed
or split into explicit logical audio/diagnostic services as those services are
migrated. A new manifest format declares `video-default` and `video-modes`;
unknown package IDs continue to work because policy is data, not an engine-ID
table.

The C64 client sends a dedicated versioned `VIDEO_SELECT` host-control command.
Firmware consumes it and never forwards it to the VM. The client enables the
raw chord detector only when the package advertises platform selectors. This
keeps AGI's existing Commodore-function-key meanings intact.

## Initial rollout scope

The first consumers are explicitly DOSVM, NESVM and DoomVM. They opt into one
package-agnostic indexed-video capability set; the service contains no switch
on those names.

| Package | Neutral source presented to MPE | Platform selectors |
| --- | --- | --- |
| DOSVM | Indexed scanlines decoded from guest CGA/Tandy state | Default, Auto-8, Enhanced-25, Sharp |
| NESVM | Indexed PPU output plus its RGB palette and source geometry | Default, Auto-8, Enhanced-25, Sharp |
| DoomVM | 320x200 indexed framebuffer plus its RGB palette | Default, Auto-8, Enhanced-25, Sharp |
| AGIVM | Existing VIC-specific cells, sprites and parser-split presentation | None; retain its existing Default/native path |

Each opted-in package can declare its own F1 default after physical comparison,
but F3/F5/F7 always mean the same direct platform selections. During migration,
preserve each package's existing visible default until its replacement passes
the same host, C64 and hardware checks. AGIVM does not pass through the generic
converter and its function-key behavior remains untouched.

## Delivery sequence

1. Build a portable, unwired indexed-video library with Color, Sharp, Auto-8
   and Enhanced-25, immutable staging, dirty records and deterministic tests.
2. Add a standalone VIDTEST module and reference frames. Do not involve an
   emulator while proving the service contract.
3. Introduce the versioned firmware video service, package capability/default
   metadata and host-owned packetization.
4. Add the shared C64 `VIDEO_SELECT` chord path and mode-state packet, initially
   exercising Color and Sharp only.
5. Add the two-screen raster kernel behind an experimental capability flag,
   then enable Auto-8 and finally Enhanced-25.
6. NESVM is integrated first; DOSVM/DOOM integration follows separately.
   AGIVM remains excluded, using its existing native solution.
7. Remove duplicated module presentation code after all installed VM packages
   use the service.

## Acceptance gates

Host tests must prove guarded workspace bounds, invalid input rejection,
immutable output under delayed ACK, dropped-frame recovery, deterministic
palette ties, mode-change full replacement, sorted/capped Auto-8 bands,
Enhanced-25 masks and stable hysteresis. Existing Color and Sharp reference
frames remain regression inputs.

Before integration, establish a Cortex-M7 conversion-time budget and remove
avoidable repeated pair scoring. The foundation is currently a synchronous
whole-frame converter; a successful compile and bounded stack do not prove it
can meet the VM scheduler's frame cadence.

C64 instruction-level tests must prove packet validation, direct selection,
held/release behavior, modifier-first release, F3 ghost consumption, no ordinary
F-key leakage, atomic screen-map publication and recovery after a rejected
packet. These tests validate logic, not raster timing.

VICE must pass PAL and NTSC whole-frame tests. Physical acceptance is separate
and mandatory on a C64 and C128 in C64 mode: PAL/NTSC raster stability, left-edge
artifacts, input ghosting, SID/input continuity, packet progress, mode switching
without mixed frames, and sustained moving video. Enhanced output does not by
itself solve VM emulation speed or cartridge-transfer throughput; those are
measured independently.
