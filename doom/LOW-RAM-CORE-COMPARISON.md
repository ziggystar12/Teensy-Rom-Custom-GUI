# Low-memory Doom core selection

Reviewed 2026-09-04 after the user supplied Doom8088 and GBADoom.

Implementation update: [GBADoom's E1M1 candidate](GBADOOM-STATUS.md) now passes
the strict ARM link with its RAM2 constant profile and bounded host runs,
including resource purging and SID effects. The inspection-only measurements
below describe the earlier selection stage.

**Use GBADoom as the preferred base for the next standalone DoomVM attempt.**
The earlier MCUME audit measures that extraction's limits; it does not establish
the minimum memory required by a different Doom engine. Evaluate GBADoom's
complete Cortex-M7 image before deciding that the host needs a larger code
window or a new flash execution profile.

## Sources inspected

| Candidate | Exact reviewed commit | Findings |
| --- | --- | --- |
| [GBADoom](https://github.com/doomhack/GBADoom) | `89097b3ff31ac1e1b2cdce9854e49726cfa462bf` | Preferred: compact ARM-oriented engine, mostly intact renderer, Doom 1 and Doom 2 IWAD support, preprocessed ROM-backed assets |
| [Doom8088](https://github.com/FrenkelS/Doom8088) | `b3073a2eb2811cd14caba6549694fc3bfcd6dd29` | Useful smaller fallback/optimization reference; derived from GBADoom, with DOS-specific memory/video and reduced features |

These upstream checkouts are under ignored `build/doom/upstream/` storage.
They were read and inspected; neither has replaced the existing module engine.

## GBADoom: concrete evidence

- `source/z_zone.c` starts its allocation search at **256 KiB** and reduces
  that request until the remaining ordinary heap can satisfy it. This is a
  maximum zone request, not an additional allocation on top of all GBA RAM.
- `source/global_data.c` allocates `globals_t` **inside that zone**. A header
  layout probe compiled with this repository's Cortex-M7 hard-float compiler
  measures it at **21,608 bytes**. Do not add it again to the zone budget.
- The same probe measures `state_t` at 28 bytes, `mobjinfo_t` at 92 bytes and
  `visplane_t` at 264 bytes. These are individual structure sizes, not counts
  or total live consumption. The state and object-definition tables are const.
- `include/doomdef.h` uses a **120 x 160** logical canvas. The GBA presentation
  code uses two **240 x 160** byte pages in VRAM. The Teensy must budget its
  own framebuffer and any copies; those cannot be treated as free GPU memory.
- `source/r_draw.c` also copies lookup tables into GBA VRAM. Those need explicit
  ordinary-RAM or read-only placement in the new target.
- `source/w_wad.c` implements `W_CacheLumpNum()` by returning a pointer into
  the const `doom_iwad` image. `source/p_setup.c` retains pointers to several
  preprocessed level lumps. A temporary SD read buffer is not an equivalent
  replacement: retained pointers require stable backing or redesigned access.
- The author's [build instructions](https://github.com/doomhack/GBADoom#building)
  use GbaWadUtil to preprocess the supplied WAD into a ROM data image. Keep
  the raw input unchanged and generated content out of tracked source.

The 288 KiB general-purpose RAM baseline is encouraging. Its separate video
RAM and addressable ROM still have to be mapped deliberately onto our host.
No whole-engine Cortex-M7 link, converted-WAD run or 1 MiB runtime acceptance
has been completed for this candidate yet.

The layout-only probe is retained in ignored
`build/doom/gbadoom-sizing/layout.c` and `layout.o`. It declares arrays sized
with `sizeof` and uses the unmodified upstream headers, `-DGBA`,
`-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard`. Its symbol sizes
are measurement markers, not an executable or a RAM allocation proposal.

## Doom8088: useful reductions and tradeoffs

The upstream 120-column flat-floor build uses a **3,840-byte openings array**
(`120 * 16 * sizeof(int16_t)`), versus the previous MCUME build's 40,960 bytes.
Its `FLAT_SPAN` path uses solid floor/ceiling colors. Its reduced, const state
tables and preprocessed map representation are useful references.

The [upstream feature list](https://github.com/FrenkelS/Doom8088#doom8088)
limits support to Doom 1 Episode 1 and omits textured floors/ceilings,
distance lighting, music, saves, multiplayer and mouse/joystick input.
EMS and XMS are optional; the default RAM path allocates available DOS
conventional memory rather than documenting an exact fixed minimum.
Its memory allocator, interrupts and assembly graphics backend would need
replacement with our native VM services. Running its DOS executable through
DOSVM is not the intended native DoomVM route.

## Next bounded implementation gate

1. Keep the generic `VmHost` boundary and strict memory tests from the MCUME
   experiment. Introduce GBADoom as a separately pinned source candidate.
2. Compile the full compact core for Cortex-M7 with replacement GBA hardware
   callbacks; separately measure code, read-only tables, static support and
   dynamic zone requirements. No firmware change before those results.
3. Convert the existing supplied shareware WAD and establish stable resource
   backing through generic host services. Measure flash capacity/placement if
   using mapped assets; SD caching requires explicit pointer-lifetime changes.
4. Budget the framebuffer, indexed-video workspace, host and stack within the
   same total 1 MiB, with no PSRAM. Run E1M1 through the module callbacks and
   then complete physical C64 input/display acceptance.

The existing [MCUME audit](MODULAR-STATUS.md) remains useful comparison
evidence and a reusable host boundary, rather than the selected final engine.
