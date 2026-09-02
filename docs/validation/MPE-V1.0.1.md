# MPE Firmware V1.0.1 verification

Release: `native09`. Firmware: `MPE_Firmware-V1.0.1.hex`, 6,172,552 bytes.

SHA-256: `6f23f596491dfa6d1601f2e0a3a27c56677d875ba7d95d592551b25e869234de`.

The integrated build used GUI revision `14ef9df71b17c058bdeba103cbe5f452d064345a`
and the nine engine sources recorded in the [release manifest](../../releases/native09/manifest.json).
Its selected GUI includes the About credits, Loading panel, and folder navigation
fixes. Both linked firmware components and all embedded GUI assets were compared
with the combined HEX. The final audit verified 342 unchanged input files.

| Check | Result |
|---|---|
| Desktop and backend suite | 173 passed |
| Release and GUI provenance | 9 passed, no skips |
| Integrated native game run | 732 frames, 285 inputs |
| Sprite wire publication | 44 shape packets, 22 complete pose commits, 646 shape-reuse frames |
| Exact C64 receiver replay | All 988 packets accepted; bitmap, color, shapes, pointers, and VIC coordinates agree |
| Native storage | 9 current and 6 legacy checks passed |
| Restart and pointer regression | 16 checks passed |
| Menu/binding/key-wait/alias regressions | 20 / 72 / 29 / 5 checks passed |
| Authored SQ1/KQ1/KQ2 ego cels | 84 checked, including KQ1/KQ2 eye pixels and occlusion |
| Main AGI catalog | 14 cartridges rebuilt; 900 startup frames checked per game |
| Public Black Cauldron demo | 5,226 loader checks, 1,343 native frames |
| Compiler 1.0.26 standalone | Extracted Scan/Build/export passed for SQ1, KQ1 and 4 MiB SQ3 |

The ARM build reserves 16,416 bytes for the MinimalBoot stack and 271,488 bytes
for RAM2 heap, above the existing build gates. Sprite shapes are cached; an
unchanged pose uses only its coordinates and colors in the frame-end packet.

These checks use the actual native source and generated 6510 receiver on the
host. They establish packet, rendering, input and packaging behavior; they do
not establish physical C64/Teensy performance or a complete playthrough of all
games. The firmware was not flashed during this release task.

New C64 menu bytecode changes the rebuilt cartridges' package/save identity.
Older saves remain for their matching older cartridges. The sprite capability
header alone does not change identity; legacy cartridges still use bitmap egos.

Detailed local proof outputs are retained in `build/native09-final-proof/`,
`build/native09-catalog-proof/`, and `build/native09-demo/`. The private compiler
checkout retains its exact receiver and bundled-compiler reports.
