# Main-character sprite restoration: source audit

AGI-64 previously used four hardware sprite slots for its main character: two
vertical body sections and two matching accent overlays. The checked original
Roger asset populated three of those slots. Restoring that approach is compatible
with the native Teensy engine; it is a future rendering change, **not implemented
by native07**, whose change is the authored dialogue/key-wait fix.

## Original implementation and actual cartridge evidence

AGI-64 commit `ecf93b94f7a2881d0b8dc549039b0b099d68ba15`, **Add SQ1 opening
room and walking actor**, contains the original implementation:

- `host/agi-view.mjs:108-142` creates `spriteTop`, `spriteBottom`,
  `spriteTopAccent`, and `spriteBottomAccent`, with a 256-byte pose stride.
- `runtime/main.ras:603-626` assigns and enables the four actor sprites alongside
  the mouse. `actor_load_frame` at line 733 copies all 256 bytes.
- `runtime/main.ras:768-802` positions each accent over its corresponding body
  section. All four share X; the lower pair starts 21 scanlines below the upper.

| VIC sprite | Original purpose | Original pointer / RAM | Opaque colors |
| --- | --- | --- | --- |
| 0 | Mouse | `$60` / `$5800` | Independent pointer color |
| 1 | Upper body | `$61` / `$5840` | Shared colors 0/1 plus body color 2 |
| 2 | Lower body | `$62` / `$5880` | Shared colors 0/1 plus body color 2 |
| 3 | Upper accent overlay | `$63` / `$58c0` | Accent color 3 |
| 4 | Lower accent overlay | `$64` / `$5900` | Accent color 3 |

The body uses multicolor pixel codes `01`, `11`, and `10`. The fourth selected
color uses code `10` in an accent sprite and leaves a transparent hole in the
body. `$d025/$d026` hold the two shared colors; `$d028/$d029` hold the body color;
`$d02a/$d02b` hold the accent color. `$d01c=$1e` selects multicolor for sprites
1-4; `$d015=$1f` enables them and the mouse. This is actual color layering,
not merely two vertical sprites or four separate animation frames.

The tracked `build/sq1-amiga.bin` at that commit confirms the generated data:
its Room 2 / VIEW 1 actor starts at byte 577938, has a 32-byte CSPR1 header and
28 poses of 256 bytes (7200 bytes total). Every pose contains nonzero upper-body,
lower-body and upper-accent bytes; the lower accent is empty in every pose.
The palette is C64 `[1,15,9,10]`. Thus **three populated sprites, with four
allocated and enabled**, is directly supported by the old cartridge.

## Why the current converter appears to use two

Commit `96ec45c98fd41523fb173c7c1991692d96207889` changed the converter to a
two-section palette and left both accent blocks zero, retaining the 256-byte
stride (`host/agi-view.mjs:191-206`). Commit
`01c9f768964e6c137243db80b43de3aae56b5c96` subsequently compacted CSPR3 poses to
128 bytes. Current `host/agi-view.mjs:312-339` emits only upper/lower sprites.
`host/agi-native-ego.mjs:133` validates that CSPR3 format; its CSPM support selects
multiple VIEW resources, rather than adding color layers to a pose.

Current KQ1 VIEW 0 has 24 cels, each 6 by 32 AGI pixels. SQ1 VIEW 0 has 28 cels,
each 7 by 32 or 33. Both fit the old two-section geometry without scaling.
The current KQ1 C64 configuration has no explicit facial-detail palette override.
The earlier four-color histogram also does not guarantee tiny eyes survive.
For semantic grouping, `host/build-agi-antic.mjs:1744-1752` contains the reviewed
Graham mapping that separates dark eyes from the light face. Reuse that intent;
its Atari color register values and PMG encoding are not C64 sprite data.

## Reuse in native MPE

1. Keep AGI execution, movement and collisions on Teensy. Remove only the eligible
   main character from bitmap composition and generate its body/accent masks
   from the original VIEW cel. Reserve sprite 0 for the existing mouse and
   sprites 1-4 for the actor; allow three active sprites when an accent is empty.
   The MPE mouse currently lives at `$4400` with pointer `$5ff8`
   (`host/mpe4-mouse.mjs:8-25`), so allocate new actor buffers in the **current**
   VIC layout instead of copying the old RAM addresses.
2. Retain source dimensions and baseline placement. Calculate the upper edge
   from `y - cel.height + 1`, then apply the current graphics offset and shake;
   position the lower pair 21 lines below it. Review a stable palette with
   explicit face/eye protection. Four slots avoid scenery cell-palette pressure,
   but do not automatically preserve every original actor color.
3. Reuse the mature standard C64 visibility rules, not the early room demo:
   `runtime/bitmap-extension.ras:1208` (`extension_ego_mask`) and line 1343
   (`extension_ego_should_mask`) handle clipping, picture priority and exact
   foreground-object opacity, including baseline ordering. The native renderer
   already has source priority, cel opacity and actor/text composition at
   `engine/native-game/mpe4_render.cpp:288-365`. Apply its visible-pixel result to
   **every** actor layer, including dialog/menu coverage. VIC's foreground bit
   alone cannot reproduce AGI occlusion.
4. Build and mask a hidden sprite buffer, then commit its pointers, coordinates,
   colors and enable bits together with the completed frame. The standard
   runtime's `actor_load_frame` at `runtime/main.ras:2931` already demonstrates
   mask-before-pointer-flip; its visibility cache starts at line 1847. Four
   double-buffered sprite shapes need 512 bytes. The current 228-byte packet
   payload cannot hold four 64-byte shapes: a full pose needs **two packets**
   plus a defined final commit. Three shapes are 192 bytes, leaving limited
   metadata room in one packet. Preserve existing CRC, sequence and ACK rules;
   an incomplete transfer must leave the previous visible pose intact.
5. Cache pose, palette and visibility. Coordinate-only movement can avoid shape
   transfers when the mask is unchanged. Keep the existing bitmap path for
   oversized/transformed cels and unsupported cases, preserving authored art.

This can reduce scenery repaint work and improve the face, but no speed gain is
measured yet. Sprite DMA affects C64 raster timing, and changing masks still
costs work. Validate the existing parser split, pointer, priority crossings and
packet retry behavior with real 6510 tests, then confirm moving characters on
the user's C64 before calling the restoration accepted.

Hardware constraints are documented in the Commodore Programmer's Reference:
[sprite dimensions and storage](https://www.devili.iki.fi/Computers/Commodore/C64/Programmers_Reference/Chapter_3/page_131.html),
[sprite priority](https://www.devili.iki.fi/Computers/Commodore/C64/Programmers_Reference/Chapter_3/page_144.html),
and [multicolor pixels](https://www.devili.iki.fi/Computers/Commodore/C64/Programmers_Reference/Chapter_3/page_179.html).
