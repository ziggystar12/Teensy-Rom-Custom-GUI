# V1.1.7 combined VM firmware and DOS shared video

**September 5 DOS correction:** the all-shared DOS path described below was
withdrawn after physical black/static screens. The current [F5 opt-in update](DOS-F5-OPT-IN.md)
keeps the original renderer/transport by default, with explicit F5 and F1 return.
Firmware and NES/GB video remain unchanged. The original shared-DOS evidence
below is historical host evidence, not physical acceptance.

Integrates main's Doom RAM2_RO96 loader (`6009933`) and NES/GB/F5 work
(`7a76c73`), plus DOS graphics service integration. Unlike the isolated V1.1.6
HEX, this release includes Doom's optional memory profile. No engine is linked
into the generic firmware; AGI remains unchanged.

## Shared service

DOS no longer runs its private C64 graphics converter. CGA/Tandy register,
palette and packed/banked VRAM decoding remain in DOS; firmware owns scaling,
C64 colors, raster plans, DMA and F1/F3/F5/F7 selection. Text remains the
existing 80-column renderer. F1 is multicolor/default; F3 Auto-8; F5 Enhanced-25;
F7 Sharp. Hold Commodore+Control with an unshifted function key, not a toggle.

An optional ABI-2 service bit 256 adds a synchronous native raster reader.
It avoids a second 32 KiB video mirror or 64 KiB byte-indexed framebuffer.
Firmware calls the reader only during initial conversion and freezes the
converted picture. DOS can continue executing and changing VRAM while DMA
and acknowledgments complete. A consumed descriptor/palette/generation stays
stable through Transferred. Older hosts reject the new DOS image's service
requirements; ordinary indexed NES/GB images and legacy guest layouts remain
supported. The full DOS guest arena is still 524,288 bytes.

Optional shared conversion hints preserve DOS's foreground-index reduction,
RGBI mapping, nonblack CGA background, and monochrome two-pixel OR reduction.
These are converter options, not game-specific heuristics. Tandy 160x200 stays
double-width. Native 320x200 fills the screen; 640x200 shrinks to 320x200.
F5/F7 narrower NES/GB sources retain their existing centering choices.

The DOS C64 matrix moved to $02D8-$02DF to avoid the shared raster state and
kernel trampoline at $02E0. Selector-only protocol $90 is separate from DOS
held snapshots $80-$8F, so Shift+Control ($83) is not mistaken for a legacy NES
video envelope. Doom's DOS-input client uses the same independent selectors.

## Checks and memory

- 192 CGA palette cases: shared F1 modes 4/5 and Sharp mode-6 reduction match
  the old DOS renderer's displayed pixels/colors exactly, including CRTC wrap.
- Tandy bank/nibble order, palette masks, blanking and wide-pixel geometry.
- Actual DOS module: FreeDOS boot, C:/D: persistence and repeated compatibility
  saves, Tandy 08/09 through all four output modes, frozen retries and return
  to text. No installed drives or private ROMs are used as writable test data.
- Generic host: live raster callbacks stop after capture, VRAM changes cannot
  modify the frozen picture, invalid ranges/generations fail, nonblack global
  background survives DMA. Existing GB four-shade centering remains tested.
- DOS C64 client: ordinary and shifted Fn keys, exact selectors, held/release
  behavior, Shift+Control routing, nonblack background and raster/matrix isolation.
- F5 destination-bank deltas: synthetic initial 12,775 bytes; repeated frame
  zero picture bytes; moving 8x8 patch 24 bytes. Not a scrolling FPS promise.
- DOS ARM: 85,200 code, 9,056 static bytes; measured RAM1 workspace 169,416 /
  187,552 (18,136 spare). Includes 36 KiB firmware video workspace. Guest 512 KiB.
- Doom remains in the separately verified 416 KiB guest + 96 KiB readonly
  constant-table profile. No Doom memory reservation is taken from DOS/NES/GB.

Physical speed, audio quality, F5 cadence/left-edge artifact and sustained bus
reliability remain hardware acceptance gates. No flashing is performed here.

The complete matched build/verification passed (`build/g7/verification.json`):
NES timing/picker/core regressions, DOS execution/save/renderer regressions,
PAL/NTSC raster plans and both clients' VICE boot, packet recovery and GUI.
The combined host uses 90,664 bytes of ITCM, reserves 49,152 bytes of stack,
and has no static RAM2 allocation. Doom's refreshed audit is loadable, including
2,100-tic level cycles and failure guards; its client boot/SID/silence checks
pass in PAL/NTSC VICE (`build/doom/e1m1-test/verification.json`).

GBVM rebuilt to exactly the existing engine SHA-256
`60b5898e71ca46c9f3a7a56ece3bf6413c346af0560cda12e0f4da8a75cf5b60`.
The supplied GB/GBC module test passed (122 pictures, picker/return, frozen
video, four shades, emulated clock, no runtime file I/O). GB/AGI ZIPs and the
immutable V1.1.6 HEX were not replaced. All published NES/DOS ZIP members were
reopened and hashed; bundled NES source independently reproduces its engine.

| V1.1.7 artifact | SHA-256 |
| --- | --- |
| MPE_Firmware-V1.1.7.hex | 7fdcd519e7918417e9da1ab61fecbd07e19541c0ac916e1e8f88f5b2d41ed8a3 |
| DOSVM-update.zip | 22e31c96608868946289952e0697c5a658b5b5d2866e84f4a462147e0438f673 |
| DOSVM.zip | 23c0c76bfbff312d696a4d26deaf31ddcb0e073fdede45b688f33e474cf4a6d2 |
| NESVM.zip | 056fe602efb35bb97389472da6fff026e1cd15e8dcc2fc2c8a5eb475efb99a98 |

## Install

Flash `firmware/MPE_Firmware-V1.1.7.hex`. Update DOS with
`vms/DOSVM-update.zip` (no drive images or D: folder), and NES with `vms/NESVM.zip`.
Retain existing games/saves. Existing GBVM.zip and AGIVM.zip remain compatible.
The generated Doom kit at `build/doom/e1m1-test/SD` is local only; its supplied
game data/upstream assets have not been cleared for public redistribution.

Retest DOS Monkey Island/Might and Magic in F1/F5/F7, then return to a prompt
and save. Compare NES Mario F5 cadence against F1/F7 without slower music/game
time. GB defaults to the centered, four-shade wide-pixel view.
