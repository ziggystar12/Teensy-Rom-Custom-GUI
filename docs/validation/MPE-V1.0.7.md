# MPE Firmware V1.0.7 validation

V1.0.7 / native15 fixes desktop text encoding and adds SD-root firmware
discovery at desktop startup. It retains the V1.0.6 menu/IRQ responsiveness
changes and the existing native AGI engine and cartridge/save formats.

## Reproduced text errors

The prior assembled C64 code produced `?? ?ard` from the real local ACME
PETSCII bytes for `SD Card`. It also produced
`dELETE THIS FILE PERMANENTLY?` from the real backend encoding of
`Delete this file permanently?`. These failures were deterministic; disabling
interrupts was not a solution.

The corrections decode local menu literals and backend serial messages according
to their respective encodings before ASCII glyph lookup. Native local messages
and raw filenames remain ASCII. The test fixture now uses the production
`ASCIItoPETSCII` table rather than supplying ASCII to a PETSCII wire channel.
It covers all printable characters, including underscore and braces. The
backend's existing backslash/grave normalization remains unchanged.

`File operation` already supplied the correct raw bytes. Its lowercase `p` glyph
was too similar to uppercase `P`; the bowl now starts at the same x-height as
`o`, with a visible descender. Font size and drawing instruction count are
unchanged. Exact glyph and final bitmap-pixel checks cover the photographed
file-operation dialog. IRQ tests retain identical interrupted/uninterrupted
pixels and enabled music/mouse service.

## Startup update behavior

The C64 invokes discovery once after its initial bitmap draw. The backend scans
only the SD root and chooses the highest strictly newer numeric major/minor/patch
version named `MPE_Firmware-Vx.y.z.hex`. FAT filename matching is case-insensitive.
Same/older versions, malformed names, directories and unrelated HEX files are
ignored. The installed comparison version comes from `DesktopFirmwareVersion.h`;
release tooling checks it against `firmware-version.json` and the GUI About text.

Discovery retains an independent candidate without changing the current menu,
path, cursor or viewport. The shared dialog displays the full captured filename
and defaults to Cancel. Only fresh explicit confirmation can reach the existing
updater. A bounded streaming CRC detects removal, size changes and same-size
replacement at confirmation and final launch. The existing HEX/board validation
still owns payload validation; discovery interprets the filename's version.

Cancel clears the capture and leaves the file for a later desktop start.
Successful installation suppresses future offers through the installed version,
without rename/delete bookkeeping. Failed optional discovery cannot leave the
normal file-launch path armed as firmware; an ordinary PRG launch after failure
is exercised. Repeated starts cannot reuse a consumed affirmative.

## Executed checks

All **263 desktop/backend/SID checks passed**, with zero failures or skips:

```powershell
node --test Source/C64/MainMenuCRT/tests/*.test.js Source/C64/MainMenuCRT/tests/*.test.mjs Source/Teensy/tests/*.test.js tests/sid-clock-reporting.test.mjs
node scripts/generate-desktop-bitmap-assets.mjs --check
```

The production backend fixture includes 30 existing firmware-target cases and
65 new discovery/version/CRC/lifecycle cases. Existing menu-map and file-operation
fixtures retain 370 and 36 cases respectively. Host flash calls are intercepted;
the tests do not program hardware.

The compact cartridge remains 7,541/8,192 bytes with its contained 7,365-byte
menu unchanged. Desktop code is 22,511/22,528 bytes, resident apps are
4,080/4,096 bytes, and the embedded desktop PRG is 26,710 bytes. All enforced
memory bounds pass. GUI source checkpoint:
`833a83fd2c1f8c16b786c9107d0568726042ee82`.

## Released artifact and native regression

`MPE_Firmware-V1.0.7.hex` is 6,192,725 bytes. SHA-256:

`0ff3bb5d38ed9f5a0a2d85c9334bb1a2e23accfc7ffc98708ef8f9f08d9b0268`

The combined-image audit verified both linked ARM images, 476 unchanged audited
inputs, the 112-file GUI snapshot, embedded GUI headers and all nine native
engine sources. All 21 release/provenance checks passed, including preservation
of native05 through native14 payloads and historical GUI snapshots. MinimalBoot
retains 16,416 bytes of stack reserve and 271,488 bytes of RAM2 heap reserve.
The [native15 manifest](../../releases/native15/manifest.json) records exact inputs.

The integrated native firmware module passed sprite and legacy bitmap runs
with unchanged SQ1 data. The sprite run completed 862 frames, 1,184 packets,
350 inputs, 64 direction reversals, 88 sprite packets and 44 commits, with zero
input interrupt masks. Both runs passed five `/SAVES` directory checks, eight
restore/fallback checks and six injected transaction failures, with no root
mutations. The Black Cauldron demo remains unchanged, SHA-256:
`a235a7925332df82dc30e9c966c7038d75b70294e8294330236346e316ff2d7c`.

Local proof files in `build/native15-final-proof`:

- `artifact-audit.json`: `8e5e05ed69d2562c3a38af6c6e66031fe2b3200e287813a8a8f20a160fb28964`.
- `firmware-native-result.json`: `57a11f50c3f0d89264d2eeefb4bbc394fadbeb0d88248fc70189ce9425ad614f`.

The desktop integration log is in `build/native15-ui-proof`. The reviewed
20-file backend patch's apply/exact-byte/reverse verification is in
`build/native15-backend-proof/verification.json`.

## Hardware boundary

The exact encoding failures are reproduced and corrected in executed software.
Other intermittent physical character corruption has no separately reproduced
shared-state cause in these checks. The firmware requires a physical C64/Teensy
test for final appearance, uninterrupted real SID playback, SD discovery and an
accepted firmware-update/reboot cycle. V1.0.7 must first be installed manually;
automatic discovery applies to subsequent releases.
