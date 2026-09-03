# MPE Firmware V1.0.11 validation

V1.0.11 / native19 makes the MHS Power Engine working memory reusable across
native modes. The firmware contains the MHS desktop, separate resident apps,
the native game engine, the MPE2 compatibility path, and native DOS. The game
engine consumes AGI-compatible cartridge resources internally, but the runtime
and user-facing product are the **MHS Power Engine**.

The source, linked image, and deterministic host checks described below pass.
No physical C64 or Teensy was flashed or controlled during this validation, so
real hardware remains the final acceptance gate.

## Shared native runtime arena

Power Engine code remains in Teensy flash. Runtime modes now share one fixed
65,536-byte, 32-byte-aligned arena in RAM2 with a 16-byte ownership record in
RAM1. This replaces separate title and MPE2 working allocations while keeping
the memory available to whichever native mode is active.

The lifecycle is explicit:

- The native title claims the arena and hands the same lease to the Power Engine
  when gameplay begins.
- The Power Engine and MPE2 compatibility path release ownership on normal
  shutdown, reset, failed launch, or lost cartridge state.
- Native DOS validates BIOS, disk, and video state before claiming the arena.
  It then closes other RAM2 heap users, quiesces background services, seals the
  arena as reset-only, and begins direct RAM2 execution. Returning from that
  state requires a Teensy reset.
- Stale leases, cross-owner releases, oversize claims, and invalid handoffs are
  rejected without clearing or corrupting another mode's storage.

The linked MinimalBoot map contains exactly one `MHSNativeArenaStorage` symbol,
65,536 bytes at `0x20206320`, and one `MHSNativeArenaControlState`, 16 bytes at
`0x20061CF4`. It contains no duplicate `MPEVirtualRAM` or
`MPE3TitleInternalAssets` allocation.

The arena ownership harness passes all 44 cases, including alignment, exact
capacity, owner handoff, stale-view rejection, release, and reset-only behavior.

## Linked memory results

| Firmware profile | Stack reserve | RAM2 heap reserve |
| --- | ---: | ---: |
| V1.0.10 MinimalBoot | 21,472 bytes | 271,840 bytes |
| V1.0.11 MinimalBoot | 21,408 bytes | 337,376 bytes |
| V1.0.11 full image | 20,992 bytes | 499,968 bytes |

MinimalBoot gains exactly 65,536 bytes of RAM2 heap reserve over V1.0.10. Its
stack reserve changes by 64 bytes and remains above the release floor. The full
firmware image retains 499,968 bytes of RAM2 heap reserve and 20,992 bytes of
stack reserve.

## Power Engine game and title checks

The combined firmware artifact audit passes against the linked firmware module.
The current sprite-capable run completes:

- 862 gameplay frames and 1,184 total packets;
- 88 sprite packets and 44 atomic sprite commits;
- 30 three-layer frames and 391 four-layer frames;
- 444 input events; and
- zero input interrupt masks.

The legacy bitmap fallback completes with 1,108 packets. Native title playback
completes with 7,244 packets before the title-to-game arena handoff. These runs
verify that removing the duplicate allocation did not remove the four-layer ego
path or the fallback renderer.

Save and input behavior is unchanged. Games save to
`/SAVES/MPE4-XXXXXXXX.sav`; existing root saves remain read-only restore
fallbacks. Keyboard, port-2 joystick, and port-1 mouse events keep the existing
queue, retry, reset, and coalescing behavior.

## Native DOS checks

The native DOS host run passes with 944 packets, 314 frames, 80 keyboard events,
two reset-separated boots, and four reset-only exits. The performance harness
passes pending-poll depths 1, 3, and 9. Text publication passes all 1,000 cells
in 53 packets. The final RAM2 layout audit validates 55 symbols, including the
shared arena and reset-only direct-memory boundary.

These checks establish that DOS performs its fallible preflight before taking
exclusive RAM2 ownership and cannot return a direct-memory session to another
native owner without reset.

## Desktop and source validation

The C64 GUI suite passes 278 of 278 tests. The Teensy suite passes all 8
top-level tests. Exact selected-GUI snapshot validation reports 27 passes and
one intentionally skipped check before release assembly.

The selected GUI source is commit
`309993c7228f23aa118c90eab4c4817c814e96f3`. Its immutable
[selected-v1.0.11 snapshot](../../gui/selected-v1.0.11) has digest:

`6b8e9e6795f81b739cba6a3c4d5bf760686bda4ea08edfbf4c8919bc077b665a`

The [native19 manifest](../../releases/native19/manifest.json) records one
native-runtime header, nine native game-engine sources, 16 compiled native DOS
sources, and 46 ordered integration patches. Final source, asset, provenance,
link-map, and combined-artifact audits pass against the release image.

## Final firmware image

`MPE_Firmware-V1.0.11.hex` is 6,336,742 bytes with SHA-256:

`87c1680a4056a3addda694dbdf0d875b8fe56b2c72cc4e35e5559674fd0ae3d5`

The immutable [native19 release](../../releases/native19) contains the firmware,
official restore image, guide, checksums, and manifest. The root `firmware`
directory contains only its README and the current V1.0.11 HEX.

## Physical acceptance still required

The release still needs a real cold boot and automatic V1.0.11 offer, Cancel and
confirmed update paths, reboot and About-version check, mouse and SID continuity
during menu/dialog work, and browsing an actual SD card. Power Engine title
playback and skipping, gameplay, menus, dialog, save/restore, sprite layering,
and repeated game resets remain physical tests. Native DOS boot, keyboard,
video, sound, and reset also remain physical hardware acceptance tests.
