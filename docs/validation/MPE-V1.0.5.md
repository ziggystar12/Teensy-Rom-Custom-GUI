# MPE Firmware V1.0.5 validation

V1.0.5 / native13 introduces shared native bitmap controls, consistent desktop
dialogs, scrolling file/text views, and filenames that preserve case and
extensions. The AGI engine, cartridge formats and save identities are unchanged.

## Desktop execution

All 227 desktop/backend tests passed with zero failures or skips. These include
executed production 6502 routines, C++ backend tests and source contract checks:

- Window, close, button, checkbox and scrollbar drawing/hit geometry.
- All 16 browser slots, row scrolling, thumb dragging and IEC directory windows.
- Open, Copy and Delete mapping to the highlighted file after scrolling.
- Fresh-input confirmation, matched mouse press/release, default Cancel,
  long filenames, loading, error and file-operation dialogs.
- Existing autolaunch, KERNAL/REU, hotkey, disk-mount and NFC commands through
  the shared bitmap dialog; cancellation and service re-enable paths.
- Bounded label and control publication, untouched pixels outside partial-byte
  edges, intact footer/frame pixels, and all 1,000 staged color cells.
- Text Viewer wrapping, line scrolling and deferred reads during thumb dragging.

The backend fixtures execute 370 menu-map scenarios, 36 file-operation cases
and 30 firmware-target checks. The latter execute the complete production
launch handler with hardware services stubbed: changed targets and repeated
unconfirmed starts cannot flash, and scrolling preserves action identity.

Two separate SID-reporting tests also passed, executing the production parser
and formatter across all 16 tune/video/TOD combinations. The test now reads the
43-column, six-row shared dialog capacity. Clock and playback code is unchanged.

Run from the repository root with ACME 0.97 available:

```powershell
node --test Source/C64/MainMenuCRT/tests/*.test.js Source/C64/MainMenuCRT/tests/*.test.mjs Source/Teensy/tests/*.test.js
node --test tests/sid-clock-reporting.test.mjs
node scripts/generate-desktop-bitmap-assets.mjs --check
node scripts/render-desktop-ui-proof.cjs
```

The native [browser](../ui-preview/native-browser.png) and
[firmware confirmation](../ui-preview/native-firmware.png) images decode pixels
drawn by the assembled C64 code against sample data. They are not mock drawings
or physical hardware screenshots. The separate interactive design preview is
documented in [UI-SYSTEM.md](../UI-SYSTEM.md).

The complete compact cartridge is 7,541/8,192 bytes; its menu is 7,365 bytes
and remains byte-identical after removing unused desktop font-cache code.
The desktop is 22,468/22,528 bytes; resident apps are 4,080/4,096 bytes. The
existing assembly/build guards remain in force. Lowercase glyphs retain the
768-byte font allocation. The preview PRG also assembled successfully.

Advanced Settings, Help and compact recovery remain standalone text interfaces.
The new desktop and normal prompts use the shared library. Moving standalone
utilities to bitmap mode requires a separate guarded load/memory adapter; they
were not silently redirected into the resident desktop's memory.

## Native game and storage regression

The exact integrated firmware module passed both sprite and legacy bitmap runs
with unchanged V1.0.2 SQ1 data. The sprite run completed 862 frames, 1,184
packets, 350 inputs, 64 direction reversals, 88 sprite packets and 44 commits,
with zero input interrupt masks. Three- and four-layer sprite frames were tested.

Both runs passed five `/SAVES` directory checks, eight restore/fallback checks,
six injected transaction failures, nine existing storage checks and six legacy
state checks. No root writes, renames or deletions were attempted.

```powershell
node tests/run-mpe4-firmware-native-harness.mjs --source build/native13/source --out build/native13-final-proof --intro PATH/TO/SQ1-64-MPE-intro.bin --raw PATH/TO/SQ1-64-MPE.bin
```

The Black Cauldron demo was not rebuilt. Its unchanged cartridge SHA-256 is
`a235a7925332df82dc30e9c966c7038d75b70294e8294330236346e316ff2d7c`.

## Released artifact

`MPE_Firmware-V1.0.5.hex` is 6,186,965 bytes. SHA-256:

`acdba317c36e022d6f3458dade1b4c2663f098772bdde7cb6f197cd799730f9d`

The final audit passed against both linked ARM images and the exact native
test result. All 454 audited inputs remained unchanged. It verified the
106-file GUI snapshot, embedded menu/desktop/Help headers, nine native engine
sources and the executed save checks. All 17 release/provenance checks passed,
including preservation of native05 through native12 payloads and GUI snapshots.

The GUI source commit is `4b74279949553b2cc83b6525f0f43db83897dfcf`.
MinimalBoot retains 16,416 bytes of stack reserve and 271,488 bytes of RAM2 heap
reserve. The [native13 manifest](../../releases/native13/manifest.json) records
source, tool, GUI and artifact hashes.

Local proof files are in `build/native13-final-proof`:

- `artifact-audit.json` SHA-256:
  `64712be85e672fe797496291811e5a89ab674bc21329efea3142c95b7652a2b1`.
- `firmware-native-result.json` SHA-256:
  `24ec7a079ab1b5ccbb6ec2d7ac8b912a49f2f1eab0d59ba4648eb322ad16e78b`.

Reproduce with `tests/run-mpe4-firmware-artifact-audit.mjs`, supplying the final
source/build, native result, raw/intro fixtures and ARM tool directory.

## Hardware boundary

This release was built and checked in software, not flashed here. Physical
C64/Teensy acceptance remains open for mouse/joystick interaction, scrolling
large SD/USB/IEC directories, dialog transitions and the actual firmware-update
cycle. Host execution does not establish VIC timing or physical bus behavior.
