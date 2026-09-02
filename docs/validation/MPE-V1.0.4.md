# MPE Firmware V1.0.4 validation

V1.0.4 / native12 writes game saves under `/SAVES`, clarifies startup clock
labels, and fixes the desktop Help page overlapping its footer. It retains
V1.0.3 desktop controls and the existing cartridge, transport, and save-state
formats. Game cartridges, including the Black Cauldron demo, are unchanged.

## Save and restore

The final firmware source was compiled into the native integration harness.
Both the sprite and legacy bitmap runs passed:

- Five directory checks: creation, reuse, a file named `SAVES`, failed
  creation, and failed directory access.
- Eight restore checks covering folder-before-root ordering, backup recovery,
  legacy state compatibility, and read-only access to old root saves.
- Six injected transaction failures, including short writes, failed read-back,
  and failed renames, preserving the previous recoverable save.
- The existing nine storage checks and six legacy-state checks.
- Zero attempted root writes, renames, or deletions.

The old V1.0.3 source fails the new directory requirement, establishing that
these checks distinguish the new implementation. The final sprite run also
passed 862 gameplay frames, 1,184 packets, 350 input events and competing-write
rejections, and 64 direction reversals. It emitted 88 sprite packets and 44
sprite commits, with zero input interrupt masks.

Reproduce from the repository root after building `build/native12`:

```powershell
node tests/run-mpe4-firmware-native-harness.mjs --source build/native12/source --out build/native12-final-proof --intro PATH/TO/SQ1-64-MPE-intro.bin --raw PATH/TO/SQ1-64-MPE.bin
```

The SQ1 fixtures are the unchanged V1.0.2 cartridge data. The final result is
`build/native12-final-proof/firmware-native-result.json`; the preliminary
old/new comparison is `build/native12-save-proof/result.json`.

## Desktop and clocks

All 199 checks across the 20 policy-listed desktop/backend test files passed,
with no failures or skips. They cover app input and close paths, mouse/menu
state, file operations, Help, controls, and loading paths. The assembled Help
page now uses exactly 19 body rows, with no line longer than 39 characters,
leaving its navigation footer intact. App locations and the read-only Text
Viewer are explained in Help and the desktop guide.

A separate app-entry probe executed nine mouse launch/close combinations
(three apps from Home, SD browser, and IEC browser) and three keyboard routes.
All returned to the expected surface. The probe remains at
`build/native11-ui-review/review-app-entry.mjs`; the current complete suite
result and log are in `build/native12-ui-proof/`.

The complete compact cartridge occupies 7,541/8,192 bytes; its contained menu
is 7,365 bytes. The expanded desktop occupies 22,503/22,528 bytes, and resident
apps occupy 4,092/4,096 bytes. All three release bounds remain enforced.

Both tests in `tests/sid-clock-reporting.test.mjs` passed. They execute the
production SID parser and formatter for all 16 tune/video/TOD combinations,
check the two displayed labels fit, and verify unchanged playback timer values.
The default tune declares PAL independently of the detected machine.
[Clock review](clock-detection-review.md) records the detector checks and
pre-existing failure cases outside this reporting change.

## Released artifact

The final combined-image audit passed against both linked ARM images and the
exact native harness result. All 386 audited inputs remained unchanged. It
verified the selected 86-file GUI snapshot, embedded GUI headers, all nine native
source files, and the executed `/SAVES` checks. All 15 release/provenance checks
passed, including preservation of native05 through native11 release payloads.

`MPE_Firmware-V1.0.4.hex` is 6,178,325 bytes. SHA-256:

`9e6ff1860181e27a73a508849651e3141756cba18077e8a6b7e370978b1f502b`

MinimalBoot retains 16,416 bytes of stack reserve and 271,488 bytes of RAM2
heap reserve. The [native12 manifest](../../releases/native12/manifest.json)
records source, tool, GUI, and artifact hashes.

The exact audit is `build/native12-final-proof/artifact-audit.json`
(SHA-256 `74d47a76f5a72928612a1cd144674c4a7ea3a6cb0e504c88379201b156c1667e`).
The harness result SHA-256 is
`3fcc3526bc230363120155db20dbd937d4053afcf48d83f8ec994ff8b48add38`.
Reproduce the audit with `tests/run-mpe4-firmware-artifact-audit.mjs`, passing
the final source/build, native result, SQ1 raw/intro fixtures, and ARM tool path.

## Hardware boundary

These are software execution, injected storage-failure, and firmware build
checks. This firmware was not flashed or physically played here. On hardware,
verify a save creates `SAVES`, an old root save restores, both clock labels
appear, and the TEENSY menu opens and closes each app normally. Text Viewer is
read-only; there is no Notepad editor in this release.
