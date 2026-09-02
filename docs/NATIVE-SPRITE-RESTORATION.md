# Main-character sprites in MPE Firmware V1.0.1

AGI-64 previously used four hardware sprite slots for its main character: two
vertical body sections and two matching accent overlays. The checked original
Roger asset populated three of those slots. **MPE Firmware V1.0.1 (native09)**
restores those four layers in the native Teensy engine and newly built game
cartridges. The Teensy still runs AGI logic, collisions, sound and rendering;
the C64 displays its main character with hardware sprites. Older cartridges
retain their existing bitmap rendering when run with the new firmware.

## Current renderer and display layout

An eligible ego cel is at most 12 by 42 AGI pixels. Its source dimensions,
mirroring and baseline remain unchanged; larger cels use the complete bitmap
renderer. Sprites 1/2 contain the upper/lower bodies and sprites 3/4 contain
the corresponding accent overlays. Only populated layers are enabled.

The two body sections share two VIC colors and each section has a private body
and accent color. This permits four colors per section, or up to six across the
character, without taking colors from the room bitmap. The palette is selected
from the unmasked source cel, so walking behind scenery cannot change it.
The compiler identifies KQ1/KQ2; their VIEW0 gray eyes use the original AGI-64
dark-detail rule instead of merging into the light face. Other source colors
retain the normal C64 mapping.

Every layer uses the same picture-priority test, ordered foreground actor
opacity, screen clipping, shake offset and opaque text-cell mask as the native
bitmap renderer. Dialogs and menus cover the character; transparent holes in
foreground actors do not. The eligible ego is omitted from bitmap composition,
leaving complete scenery behind it. Sprite 0 remains the mouse.

| Use | VIC RAM | Sprite pointers |
| --- | --- | --- |
| Mouse, unchanged | `$4400` | `$10` |
| Ego pose bank A, four shapes | `$4500..$45ff` | `$14..$17` |
| Ego pose bank B, four shapes | `$4600..$46ff` | `$18..$1b` |
| Screen and sprite-pointer table | `$5c00`, `$5ff8..$5fff` | Current VIC bank `$4000` |

Each bank contains four 64-byte shapes in upper body, lower body, upper accent,
lower accent order. The lower pair is positioned 21 scanlines below the upper.
The upper origin is `x = 24 + 2*(ego.x + shake)` and
`y = 50 + ego.y - cel.height + 1 + graphicsTop + shake`.

## Packet contract and backward compatibility

The checked `M4G1` package header flags at byte 32 declare the terminal's
capabilities. Bit 0 remains original-startup mode; bit 1 enables ego sprites.
Bits 8–9 select palette profile 0 (generic), 1 (KQ1), or 2 (KQ2). Profile 3,
unknown flag bits and any nonzero profile without sprite capability are rejected.
These flags are protected by the existing package header checksum. The sprite
capability alone leaves save identity and the 9,624-byte saved AGI state unchanged.
The V1.0.1 cartridge rebuild also adapts menu bytecode for C64 controls, which
changes package identity and its save filename. Keep older saves with their
matching older cartridges; renaming them is not a safe migration.

Changed shapes are sent before that frame's bitmap cells in two ordinary checked
M3 packets: type **5**, flags `$20`, payload length **130**. Each payload contains
version **1**, part number **0** or **1**, then exactly 128 shape bytes. The
receiver writes both halves to the hidden bank. CRC/sequence/ACK handling is
unchanged; an incomplete transfer cannot replace the visible pose.

The final type-2 SID packet keeps its original 26 sound bytes and appends this
11-byte descriptor, for payload length **37**:

| Descriptor byte | Meaning |
| --- | --- |
| 0 | Version 1 |
| 1 | Enable mask, bits 1–4 for the corresponding VIC sprites |
| 2–3 | Upper X coordinate, little endian; bit 8 is the only valid high bit |
| 4 | Upper Y coordinate; the lower pair adds 21 |
| 5–6 | Shared colors, `$d025/$d026` |
| 7–8 | Upper/lower body colors, `$d028/$d029` |
| 9–10 | Upper/lower accent colors, `$d02a/$d02b` |

At that frame boundary the receiver validates that it has either no pending
shape parts or both parts, waits at its existing display boundary, and commits
the pointers, coordinates, colors and enable bits together. Shape packets add
no frame wait. If only position or colors changed, no shape packets are needed;
the descriptor reuses the current bank. A zero enable mask hides the ego for
text screens or completely occluded poses. Mouse register bits remain separate.
Cartridges without bit 1 continue using 26-byte SID packets and bitmap actors.

## Validation and performance boundaries

The native renderer tests cover all 28 SQ1, 24 KQ1 and 32 KQ2 VIEW0 cels. Every
tested cel uses an accent layer: SQ1 has 22 three-layer and six four-layer poses;
KQ1 and KQ2 each use three populated layers. All 18 KQ1 and 24 KQ2 gray eye pixels
in those cels remain dark. Tests independently decode the shapes and check
foreground transparency, text masking, clipping, the 12-by-42 boundary,
oversized-cel fallback, coordinate-only transfers and old-cart compatibility.

The existing bitmap regression remains byte-exact across 73 SQ1 pictures,
73 overlays, 1,652 cels, 132 title frames and 108 motion frames. The renderer
state occupies 656 bytes and the native Session occupies 59,584 bytes in the
host harness, within the existing 64 KiB arena. The firmware integration
harness checks the actual packet producer, both shape halves, SID descriptors,
publication and ACK stalls; it also repeats gameplay and storage checks with
the sprite capability removed from the same game package. The 6510 receiver
has separate instruction-level tests.

The integrated SQ1 run passes 732 native frames and 285 input events in each
mode, including nine storage checks and six legacy-save migration checks. The
sprite run publishes 22 complete poses in 44 shape packets and reuses its
existing shapes for 646 visible frames. The legacy run emits no sprite packets.
The exact source hashes and packet traces are recorded by
`tests/run-mpe4-firmware-native-harness.mjs`; generated reports remain build
artifacts rather than additional firmware downloads.

The sprite path removes ego repainting from the room bitmap and omits shape
transfers when masks and animation are unchanged. No physical speed improvement
is claimed: sprite DMA and changing masks still cost work. Host/compiler tests
do not establish real C64 raster timing, mouse behavior or gameplay acceptance.
Confirm movement, foreground crossings, dialogs and parser text on hardware.

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

## Why the standard converter used two

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

## Source references used for the restoration

- `runtime/bitmap-extension.ras:1208` (`extension_ego_mask`) and line 1343
  (`extension_ego_should_mask`) establish the mature C64 clipping, picture
  priority and exact foreground-opacity behavior, including baseline ordering.
  VIC's foreground bit alone does not reproduce these AGI rules.
- `runtime/main.ras:2931` (`actor_load_frame`) demonstrates masking the hidden
  pose before flipping sprite pointers; its visibility cache starts at line 1847.
- `host/mpe4-mouse.mjs` establishes the current mouse/VIC layout. The new
  `host/mpe4-ego-sprites.mjs` receiver uses this layout and the packet contract
  above. The native implementation is in `engine/native-game/mpe4_render.cpp`,
  `mpe4_session.cpp`, and `mpe4_firmware.h`.

Hardware constraints are documented in the Commodore Programmer's Reference:
[sprite dimensions and storage](https://www.devili.iki.fi/Computers/Commodore/C64/Programmers_Reference/Chapter_3/page_131.html),
[sprite priority](https://www.devili.iki.fi/Computers/Commodore/C64/Programmers_Reference/Chapter_3/page_144.html),
and [multicolor pixels](https://www.devili.iki.fi/Computers/Commodore/C64/Programmers_Reference/Chapter_3/page_179.html).
