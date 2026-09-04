# Build provenance

The current **V1.0.19 / native27** build presents TeensyROM's three components:
the GUI, MHS Power Engine, and DOSVM. DOSVM adds an 80-column monochrome
console, visible POST and cursor, paced packet recovery, and optional sharp
320x200 CGA rendering while retaining its default multicolour renderer,
visible scrolling and writable drives.
The source and output records are in
[`releases/native27/manifest.json`](../releases/native27/manifest.json),
[`docs/firmware/source.lock.json`](firmware/source.lock.json), and the current
[checksum ledger](firmware/SHA256SUMS.txt).

The GUI updater no longer issues separate SD status/CMD13 commands while
fingerprinting a HEX file. Identity, size, clean EOF, cancellation and CRC
checks remain enforced. If an older installed GUI reports “Firmware selection
changed” for an unchanged file, press **V** and install V1.0.19 once through
the original text menu. The new GUI path becomes available after reboot and
was confirmed working with V1.0.15 by the user.

DOSVM R23 retains R22's direct-memory speed and paced quiet
retry after any failed packet read, including bootstrap. The firmware finishes
the current VM slice before signalling retry readiness. The C64 then measures
two frames directly from the live VIC raster, which is already running before
the terminal enables raster interrupts, and rereads the same packet. A matching
acknowledgement releases normal execution. CRC and bounded retry validation
remain. R16's photo shows fixed signature bytes corrupted by XOR `08`, without
proving publisher mutation. The user confirmed V1.0.15 DOSVM startup and Might
and Magic working. Physical R20 exposed a pre-IRQ frame-counter timeout; R21
avoided that wait but exhausted immediate bootstrap rereads with error `0C`.
Both cases are executable regressions corrected by R22, whose cold start and
gameplay are physically confirmed. R23 corrects the DOS backslash glyph in the
CRT receiver without changing firmware, drives, input, or game graphics.
See [DOS hardware checks](../dos/HARDWARE-TEST.md).

The release adds a five-row browser without a bottom status strip, centered
message/loading modals, fill-style loading, and desktop drag ghosts with a
visible snap grid. Native Appearance, Input, and Storage panels provide the
release's display, controller, and capacity settings. Snake, Calculator, and
Text Viewer remain bundled in the single HEX but load into the shared `$C000`
app space only when launched. The compact original interface provides boot and
recovery while the desktop, settings, and apps reuse C64 memory at different
times.

Native27 also includes every M4G2 AGI runtime change from the immutable
V1.0.18/native26 release: independent Fastest scheduling, compact predecoded
ego VIEW sidecars with checked raw fallbacks, unchanged-presentation suppression,
and twelve stable per-game save slots with validated replacement and backup
recovery. It retains `/SAVES`, F1 Help, IEC disk boot, the Music panel, and the
existing game ABI. MHS Power Engine code remains in flash. Title, active AGI sessions,
legacy MPE2 and DOS share one 64 KiB RAM2 arena. Reusable modes release it on
exit; DOS seals it for reset-only direct execution.

## Current source pins

| Input | Pin |
| --- | --- |
| Public firmware / profile | `1.0.19` / `native27` |
| Selected GUI | `PENDING: gui/selected-v1.0.19/` |
| GUI source commit | `PENDING` |
| GUI content digest | `PENDING` |
| Ordered integration patches | `0001` through `0047` |
| TeensyROM upstream | `3436b8fbd7c642ef9eabc691d3d09da08a6a6690` |
| Arduino CLI / Teensy core / CRC32 | `1.4.1` / `1.61.0` / `2.0.0` |

The V1.0.19 snapshot is pending. Once created, its provenance file will lock
every required GUI source, test, and generated header. Its reviewed backend
patch and policy remain under `engine/custom-gui/`. After applying the 47 integration
patches, the builder incorporates the GUI, nine native game-runtime sources,
20 native DOS sources and one shared native-runtime source. Manifests hash
those inputs separately. Exact output size, firmware hash and linked memory
reserves come from this build's records, not from the V1.0.12 image.

The desktop development tree under `Source/` must be incorporated into a new
locked snapshot before a native release uses changes there. Backend changes
also need a matching reviewed patch and policy. Merely changing `Source/`
does not change the pinned release inputs.

From the exact `engineCommit` recorded in the current source lock, build with:

```powershell
.\scripts\build-firmware.ps1 -CustomGuiAcmePath C:\Tools\ACME\acme.exe -OutputRoot build/native27
```

The builder checks the patch chain, snapshot and generated headers, runs
conformance checks, builds both firmware halves, and verifies memory reserves.
It does not flash hardware. See [root build instructions](../README.md#build-the-combined-firmware-on-windows).

After validation, create the release once:

```powershell
node scripts/create-native-release.mjs --build build/native27 --release native27
```

The publisher checks the image and source hashes and refuses to overwrite an
existing release or update a separate compiler checkout. Native05 through
native26 releases and earlier selected GUI snapshots remain immutable and
reproducible from their recorded commits. [Native06 storage](NATIVE06-STORAGE.md)
and [Native07 input](NATIVE07-INPUT.md) describe features retained by the current
release.

[`releases/native26/manifest.json`](../releases/native26/manifest.json) remains
the immutable source and output record for the V1.0.18 M4G2 AGI release.
Native27 carries those runtime changes forward without modifying that kit.

## Public version numbers

[`firmware-version.json`](../firmware-version.json) controls the public version,
internal profile and exact GUI snapshot. The builder and release tool derive
`MPE_Firmware-V1.0.19.hex` from `1.0.19` and reject a GUI whose About or backend
discovery version differs. Manifests record both the upstream TeensyROM and
public MPE versions, plus the version-configuration checksum.

For this release, advance the patch version to `1.0.19`, select the `native27`
internal profile, and update development About text and
`Source/Teensy/DesktopFirmwareVersion.h`. Rebuild and commit GUI inputs before
exporting a new immutable snapshot:

```powershell
node scripts/snapshot-custom-gui.mjs --commit COMMIT --destination gui/selected-v1.0.19 --acme C:\Tools\ACME\acme.exe
```

The snapshot reads exact Git blobs and records reviewed sources, tests,
references and generated headers. Update `firmware-version.json` with the
snapshot path, commit and digest, update current download guidance, then build
and validate. Existing snapshots and release kits remain unchanged. The root
`firmware/` folder contains only the current HEX and README; supporting hashes
and locks belong under `docs/firmware/`.

## Preserved native05 release

The source migration preserves the bytes of all 36 patches, all nine native
engine files, the pinned vrEmu6502 dependency, and the 49 selected GUI inputs
used for the verified native05 firmware. File hashes are recorded in
`releases/native05/manifest.json` and `gui/selected-e305/provenance.json`.

| Input | Pin |
| --- | --- |
| TeensyROM | `3436b8fbd7c642ef9eabc691d3d09da08a6a6690` |
| Custom GUI | `e305f6dc24c526b1e337e9718fbb71d599ed70d8` |
| GUI content digest | `c574929263728ebae17064bbe5a7d48941b33db931f62121476734cb25eda7a3` |
| GUI backend patch | `66d0c3070ff3a20cb1abbb669c6280d4a3f46131cc804fa24691c661942ffc48` |
| vrEmu6502 | `aae98cb14386d832cb7357c99626520b6590bc24` |
| Arduino CLI | `1.4.1` |
| Teensy core | `1.61.0` |
| CRC32 library | `2.0.0` |
| ACME used for native05 | `0.97`, SHA-256 `dfe1ea314a1d66854999308834a4636e7cfd1507ed21ba689dc9f00ac8051957` |

The maintained GUI backend policy's reviewed commit can predate the selected
GUI snapshot: it pins the eight backend file contents. The selected e305
snapshot passed that policy unchanged. The remaining menu inputs are pinned
by the complete source provenance lock, including generated asset headers.

The builder clones the pinned upstream, stages the checksum-verified vendor
files before patch 0014, applies patches 0001 through 0036 in order, copies the
native engine, and incorporates the selected GUI. It assembles the GUI and
checks its bytes, runs tests against the actual applied source, and builds the
Fab0.4 dual image. `SOURCE_DATE_EPOCH` is derived from the upstream commit so
embedded build date/time strings remain deterministic.

The `native05-exact` profile explicitly restores the immutable vrEmu6502 vendor
files after the historical patch chain. The released 05 build used that source:
its earlier builder re-staged the original vendor on repeated builds, undoing
patch 0014's dispatch-table placement in flash. A fresh application of that
patch would otherwise move 3,072 bytes out of DTCM and change the firmware.
The standalone profile preserves the actual released RAM placement and records
the compiled vendor hashes. This resolves the old fresh/repeated-build
difference without changing the released image or native gameplay sources.

Native05 custom firmware SHA-256:

`abbbedf426ffc085fdd17396bc191223a3274524e16e3880cd42c5cfb508fe78`

Official restore firmware SHA-256:

`575ab4e237b1c9d5539e8d56248490dd471c6e368d2c98fd66311dddb65252bf`

The retained build reserves 16,416 bytes for the MinimalBoot stack and
273,536 bytes of RAM2 heap. The native session uses 58,928 bytes inside the
retired intro arena. The builder rejects results below its stack/heap limits.

The native05 SQ1 source-input route reached the original winning ending and
all credits; KQ1 smoke coverage reached its original starting room, walking,
LOOK and mouse movement. These are deterministic native tests. The physically
accepted 04 checkpoint remains separate from later 05 changes; a firmware build
or source migration is not a new physical hardware acceptance result.

To update the selected GUI, export a reviewed source revision with all required
files and update its provenance lock. Backend changes require a corresponding
policy/backend-patch review. Merely changing the enclosing engine Git HEAD
does not change the recorded upstream GUI identity.
