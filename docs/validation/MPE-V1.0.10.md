# MPE Firmware V1.0.10 validation

V1.0.10 / native18 completes the five Teensy desktop improvements selected for
this release: native input buffering, bounded UI repaint work, scalable
directory handling, cached SD mounting, and safer automatic firmware discovery.
It also retains the current native AGI sprite engine and the native DOS work
already present on `main`.

The software, source, image, and deterministic host checks described below pass.
No physical C64 or Teensy was flashed or controlled during this validation, so
real hardware remains the final acceptance gate.

## 1. Native input buffering

Keyboard input now uses a 16-entry FIFO. Joystick state keeps the newest
direction plus a 16-bit fire count. Pointer motion is coalesced while eight
ordered button edges are retained. If the C64-side queues are full, the exact
wire event is retried rather than silently replaced. Reset clears every buffered
input state.

The integrated native AGI run exercises 444 input events, 17 keyboard edges,
56 pointer samples, 11 pointer edges, two fire edges, two counter-wrap checks,
two exact queue-full retries, and reset clearing. It reports zero input interrupt
masks in both sprite and legacy-bitmap runs.

## 2. Bounded desktop repaint work

Dialogs, menus, file windows, selection changes, and music controls now update
only the affected screen regions. The retained NMOS 6502 model records the full
measurements in [UI_PERFORMANCE.md](../../Source/C64/MainMenuCRT/UI_PERFORMANCE.md).
Choice repaint falls from 476,772 to 99,867 modeled cycles. Menu operations take
87,464 to 141,536 instructions, and Music Play/Pause changes state in 55 cycles
without publishing a full repaint. Drawing-time IRQ masking remains zero in the
measured paths, allowing the real mouse and SID IRQ wedge to continue running.

The firmware-update preflight remains cancellable with STOP or a fresh click for
ten 2.9-second activity sweeps. The actual flash move is deliberately
non-cancellable once armed.

## 3. Scalable directory handling

The shared directory builder orders parent, directories, then files using a
deterministic case-insensitive O(n log n) sort while preserving displayed case.
File type classification is case-insensitive, and stable names use bounded 4 KiB
pools. The 4,000-entry host scenario completes in 62,074 comparisons and remains
inside the configured storage bounds.

## 4. Cached SD mounting

All desktop SD access now shares one mount state. The card-detect DAT3 input is
allowed to settle before sampling. An empty socket avoids an expensive mount
attempt, a live mount is reused, and failures are cached for the current media
generation with bounded retry. Explicit Refresh and card reinsertion advance the
generation and permit a new mount.

The directory/SD harness passes 13 scenarios, including empty socket, insertion,
reuse, failure caching, explicit refresh, reinsertion, and the 4,000-entry case.

## 5. Firmware discovery and update safety

Cold desktop startup performs a filename-only scan of the SD root before showing
the first screen. It selects only a strictly newer
`MPE_Firmware-Vx.y.z.hex` candidate. The full file CRC is deferred until the user
accepts the update, then read in 1,024-byte chunks with cancellation and media
generation checks.

The Intel HEX parser rejects malformed records, missing or trailing data after
EOF, out-of-range addresses, and source changes. It recomputes the raw-file CRC
immediately before flashing. Manual updates from SD or USB use the same captured
path and checksum contract. A Cancel that arrives between Begin and checksum
retrieval aborts the captured GUI update and cannot fall through to the older
unchecked path. The original text recovery updater remains available.

The Teensy suite includes 32 firmware-target checks and 77 discovery checks.
The parser harness also passes. The updater never renames or deletes a candidate.

## Desktop apps remain separate

Snake, Calculator, and the Notepad slot (currently the read-only Text Viewer)
remain in the separate resident `GeosApps` payload. The desktop core provides
launching, windows, input, drawing, and file services; app behavior is kept out
of the core. The boundary is documented in
[DESKTOP-APPS.md](../DESKTOP-APPS.md) and [UI-SYSTEM.md](../UI-SYSTEM.md).

The assembled payloads remain inside their limits:

| Payload | Bytes | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Desktop code | 22,506 | 22,528 | 22 |
| Resident apps | 4,093 | 4,096 | 3 |
| Compact cartridge (`TeensyROMC64.bin`) | 7,559 | 8,192 | 633 |

## Source checkpoint and release contents

The selected GUI source checkpoint is
`59dddf96aa572ce90bca41760124db74da2df601`. Its 140-file snapshot is recorded
in [gui/selected-v1.0.10](../../gui/selected-v1.0.10) with digest
`d5d71232599577da89c4daba4cd88caf43b24e5a547a5fcacbd7e41a647b68e0`.

The reviewed backend patch is 120,224 bytes with SHA-256
`d52519415b6efafdd0c1a3c31a892be485be57462eba48303e3bb6af81ac2e32`.
The GUI selection policy has SHA-256
`51165c024784333f8c85364d54df6603f112275fc2a0eb50eafeffe2aed0a737`.

The immutable [native18 manifest](../../releases/native18/manifest.json) records
three release artifacts, nine native AGI sources, 16 compiled/staged native DOS
sources, 45 ordered patches, five vendored files, three compiled vendor files,
and five build tools. The two `mpe5_paged_memory` files retained in the repository
are legacy comparison/test sources and are not part of the 16-source compiled
native18 inventory.

## Final firmware image

`MPE_Firmware-V1.0.10.hex` is 6,333,862 bytes with SHA-256:

`611a38b72e5fc8521dcab4bcbe465dfd18ed95cd684082a7ed25c93a5f8d44cd`

MinimalBoot retains 21,472 bytes of stack reserve and 271,840 bytes of RAM2 heap
reserve. The full image retains 20,992 bytes of stack reserve and 499,968 bytes
of RAM2 heap reserve. The release directory contains the firmware, official
restore image, guide, checksums, and manifest. The root `firmware` directory
contains only its README and the current V1.0.10 HEX.

The complete desktop extracted from the released HEX passes three exact text
replays: SD Card, 16 filenames with menu restoration, and the updater message
glyphs. The extracted desktop SHA-256 is
`dae4534d4868fcd1c0f01f6c66a7354fce9bd21cbd6c21b2431ad93b34b0b1c0`.

The combined artifact audit passes and is retained in
`build/native18-final-proof/artifact-audit.json`, SHA-256
`bf593c397f3661c2bd971fd7038e8996e8631725e77754694e9ff08265f5dc16`.

## Native AGI and DOS checks

The integrated AGI harness uses the actual firmware module with simulated SD
and bus pins. The sprite run completes 862 frames, 1,184 packets, 88 sprite
packets, 44 atomic commits, 754 coordinate frames, 798 visible-sprite frames,
30 three-layer frames, 391 four-layer frames, and 64 direction reversals. The
legacy fallback completes the same 862-frame session with no sprite packets.
Both paths pass the save-directory and transactional failure cases without a
root write or mutation.

Focused ego checks pass for SQ1, KQ1, and KQ2, including source visibility,
text masks, coordinate skips, clipping, and legacy fallback. KQ1 preserves 18
gray eye pixels and KQ2 preserves 24 in the tested frames.

The native DOS checks pass direct-memory, paged-memory regression, publication,
speaker, linked RAM2 ownership, integrated reset/boot/DIR, latency, and three
pending-poll performance settings. The integrated session completes two
reset-separated boots, 944 packets, 314 frames, 80 keyboard events, at most four
SD operations per slice, 236 Boulder CGA frames, 60 audible SID frames, and two
native Sierra frames. The host model reports 112,120 microseconds to boot, a
927-microsecond maximum foreground slice, and a modeled 501-microsecond
ACK/input return.

SID reporting tests confirm that tune metadata and detected C64 hardware are
separate values, so a PAL tune declaration does not change startup hardware
detection or playback behavior.

## Completed checks and validation integrity

The current C64 suite passes 278 of 278 tests. The Teensy suite passes 8 of 8
top-level tests, including the firmware, discovery, directory, SD, and parser
scenarios. Boot routing passes 2 of 2, GUI source/provenance passes 28 of 28,
released-HEX text replay passes 3 of 3, and SID reporting passes 2 of 2. Asset
drift, direct-memory, paged-memory regression, publication, speaker, linked RAM2,
integrated DOS, DOS performance, AGI integration, and the final artifact audit
also pass.

Two validation harness corrections are part of this release. The AGI harness
binds patch 0035 by its exact hash and order, then verifies the final live route
after later patches modify the same lines. The artifact audit checks the new
queue, retry, wrap, and reset contract instead of looking for the obsolete
single-producer field, and accepts a Windows UTF-8 BOM while still hashing the
manifest's exact bytes. These changes preserve the checks rather than weakening
them.

## Physical acceptance still required

The release still needs a real cold boot with the card already inserted, the
automatic V1.0.10 offer, Cancel and confirmed update paths, reboot and About
version check, mouse and SID continuity during menu/dialog work, and browsing an
actual large SD directory. Native AGI walking, menus, dialog, save/restore and
sprite layering, plus native DOS boot, keyboard, video, sound and reset, also
remain physical hardware acceptance tests.
