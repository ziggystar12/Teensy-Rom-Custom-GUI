# MPE Firmware V1.0.12 validation

V1.0.12 / native20 polishes the MHS desktop About window while retaining the
native MHS Power Engine, reusable native runtime arena, separate resident apps,
firmware updater, MPE2 compatibility path, and native DOS from V1.0.11.

The source, assembled desktop, linked firmware, and deterministic host checks
described below pass. No physical C64 or Teensy was flashed or controlled
during this validation, so real hardware remains the final acceptance gate.

## About window

**TEENSY > About MPE Firmware** now uses the same shared window frame and close
X as the other desktop windows. A pointer click outside the X leaves About open;
a click on the X closes it. The old on-screen closing instructions are gone.

The window identifies **MPE FIRMWARE V1.0.12**, credits **John Swiderski** and
**Mean Hamster Software**, and displays `www.MeanHamster.Com`. The focused
assembled-desktop checks cover the shared frame, X hit area, outside-click
behavior, version, credits, and website copy.

## Firmware discovery and the V1.0.9 fallback

The supported SD-root filename for this image is exactly
`MPE_Firmware-V1.0.12.hex`; no alternate name is required. Matching is
case-insensitive and accepts the three-part numeric version form. The host
firmware suite passes 32 target checks and 77 discovery checks, including
strictly-newer selection, root scanning, validation, and rejection cases.

A physical Teensy running V1.0.9 has failed to display its automatic update
offer even with the correctly named file in the SD root. V1.0.12 cannot change
the scanner inside an already installed V1.0.9 image. For that installation,
open or refresh SD, select `MPE_Firmware-V1.0.12.hex`, and confirm Update once.
V1.0.11 and later defer the full payload CRC until confirmation and use stronger
SD mount settling and bounded retry. Automatic detection by those versions is
still a physical hardware acceptance item.

## Desktop build checks

The complete C64 GUI policy suite passes 286 of 286 tests. The three focused
assembled suites for publication, widgets, and firmware startup pass 49 of 49
tests. The rebuilt desktop remains within every fixed payload boundary:

| Payload | Bytes | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Desktop code | 22,513 | 22,528 | 15 |
| Resident apps | 4,093 | 4,096 | 3 |
| Compact cartridge (`TeensyROMC64.bin`) | 7,559 | 8,192 | 633 |

The selected GUI source is commit
`9e42e3a0264ef9e7b3ea7866e8930fcc58a090d3`. Its immutable
[selected-v1.0.12 snapshot](../../gui/selected-v1.0.12) has digest:

`868e5222911ae44e2eeaf9b19018c99dfbea9a61a242393ca8b8ee73bcd668ca`

The native20 manifest records 140 selected GUI files, 46 ordered integration
patches, one shared native-runtime header, nine native Power Engine sources,
and 16 compiled native DOS sources.

## Power Engine and memory compatibility

This release does not change the MHS Power Engine cartridge or save formats.
Existing V1.0.2-or-later MPE cartridges do not need rebuilding. Saves remain at
`/SAVES/MPE4-XXXXXXXX.sav`, with prior root saves available as read-only restore
fallbacks.

MinimalBoot retains 21,408 bytes of stack reserve and 337,376 bytes of RAM2 heap
reserve, above the 327,680-byte release floor. The full image retains 20,992
bytes of stack reserve and 499,968 bytes of RAM2 heap reserve. Title playback,
an active game, MPE2, and native DOS continue to share the single 65,536-byte
runtime arena introduced in V1.0.11. The arena ownership harness passes all 44
cases, including claim, handoff, release, stale-owner rejection, capacity, and
reset-only behavior.

## Linked MPE4 and title checks

The sprite-capable MPE4 run completes 862 gameplay frames in 1,184 packets,
with 444 input events, 88 sprite packets, 44 atomic sprite commits, 30
three-layer frames, 391 four-layer frames, and zero input interrupt masks. The
legacy bitmap fallback completes in 1,108 packets. Native title playback
completes in 7,244 packets before handing the shared arena to gameplay.

## MPE5 and native DOS checks

The integrated MPE5/native DOS run completes 944 packets, 314 frames, and 80
keyboard events across two reset-separated boots and four reset-only exits. Its
performance checks pass pending-poll depths 1, 3, and 9. Text publication covers
all 1,000 cells in 53 packets, and the final RAM2 layout audit validates 55
symbols.

## Final firmware image

`MPE_Firmware-V1.0.12.hex` is 6,336,755 bytes with SHA-256:

`fd31dcc2d6dc84fddacaa6f18f2c12ef18a6113f58f672346c7d475e32ccf309`

The build manifest independently records that size and digest. The root
firmware file was hashed again and matches both values.

## Physical acceptance still required

The release still needs a real manual update from V1.0.9, reboot, and About
version/appearance check. A cold boot from V1.0.11 or later should also verify
the automatic V1.0.12 offer, Cancel and confirmed update paths. Mouse and SID
continuity during menus and dialogs, SD browsing, Power Engine title playback,
gameplay, menus, dialog, save/restore, sprite layering, repeated resets, and
native DOS boot/input/video/sound/reset remain physical hardware tests.
