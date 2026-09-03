# Build provenance

The current V1.0.10 / native18 build improves desktop response, SD directory
handling, firmware discovery and update validation. It retains the shared
bitmap controls, scrolling views, `/SAVES`, F1 Help, IEC disk boot,
Control/Music panels and the existing game ABI. The exact GUI revision is
pinned in `firmware-version.json`. Its source and output hashes are recorded in
[`releases/native18/manifest.json`](../releases/native18/manifest.json).

The selected GUI inputs are locked in `gui/selected-v1.0.10/provenance.json`;
the reviewed backend patch and policy are in `engine/custom-gui/`. The native
build applies patches 0001 through 0045 in order to the pinned upstream, then
incorporates the selected GUI, nine native AGI engine sources and 16 native DOS
sources. The release manifest hashes each of those inputs separately.

Run `scripts/build-firmware.ps1` from the repository root to reproduce this
combined firmware. The builder assembles the selected GUI and verifies its
generated menu, desktop, and Help headers. The desktop development sources under `Source/` are kept
in the same repository, but changes there must be incorporated into the locked
GUI snapshot before a new native release uses them. Backend changes also
require a matching reviewed patch and policy. See
[the root build instructions](../README.md#build-the-combined-firmware-on-windows).

[Native06 storage](NATIVE06-STORAGE.md) documents the SD-only extended
cartridge mapping. [Native07 input](NATIVE07-INPUT.md) describes the corrected
authored `have.key` waits retained by later releases. The native05 through
native17 releases remain unchanged and can be reproduced from their recorded
source commits.

From the locked native18 source checkout, reproduce the build with:

```powershell
.\scripts\build-firmware.ps1 -CustomGuiAcmePath C:\Tools\ACME\acme.exe -OutputRoot build/native18
```

After validation, `scripts/create-native-release.mjs` verifies the built image
and current source hashes before creating a release directory:

```powershell
node scripts/create-native-release.mjs --build build/native18 --release native18
```

Rerunning that publication command against an existing release is intentionally
refused.
The release tool also refuses to update a separate compiler checkout. The
compiler kit pins the release and its engine source commit.

## Public version numbers

[`firmware-version.json`](../firmware-version.json) is the source of truth for
the public version, internal release id, and exact GUI snapshot. The builder
and release tool derive `MPE_Firmware-V1.0.10.hex` from version `1.0.10`. Both
reject a GUI whose About or backend discovery version does not match. The build
manifest retains the upstream TeensyROM version separately and records the
public version as `mpeFirmwareVersion`, together with the version configuration
checksum.

For the next firmware release, increase the final number to `1.0.11`, select a
new internal release id, and update the development About text and
`Source/Teensy/DesktopFirmwareVersion.h` to the same version.
Rebuild the GUI headers and commit those GUI inputs before exporting
them into a new immutable snapshot:

```powershell
node scripts/snapshot-custom-gui.mjs --commit COMMIT --destination gui/selected-v1.0.11 --acme C:\Tools\ACME\acme.exe
```

The snapshot command reads exact Git blobs, checks the reviewed backend, and
records all required source, test, reference, and generated-header hashes.
Update `firmware-version.json` with that snapshot's path, commit, and digest.
Update current download links and the firmware guide, then build and audit the
new image. Existing `gui/selected-*` snapshots and `releases/native*` kits stay
unchanged. The public `firmware/` folder contains only the current HEX and its
README; checksums and source locks belong in `docs/firmware/`.

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
