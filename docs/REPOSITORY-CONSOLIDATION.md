# GUI and Power Engine repository consolidation

The maintained repository for the TeensyROM custom desktop and native MHS
Power Engine is [Teensy-Rom-Custom-GUI](https://github.com/ziggystar12/Teensy-Rom-Custom-GUI).
The former separate engine repository was merged into this repository with
both projects' commit histories preserved.
The canonical local workspace is `E:\MHS-Repository\Teensy-Rom-Custom-GUI`.
Use this repository for both GUI and MPE development, builds, commits, and syncs.

| Imported history | Last commit before consolidation |
|---|---|
| Custom GUI and delivered native08 firmware | [c892858](https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/commit/c892858b0a87072279c0de1cc7d88e8a5867a3b4) |
| Native engine, reproducible builds, releases, and demo | [1ab92a3](https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/commit/1ab92a3654a61af1e3b0d6bdc50b98186a8a9fe4) |

The import retains the `engine/`, `gui/`, `releases/`, `tests/`, and `Demo/`
paths and the native build scripts. Native05 through native08 release files,
selected GUI snapshots, and the Black Cauldron CRT retain their original
bytes and checksums. Their recorded historical source commits remain
reachable through this repository's merged history.

Use [the firmware index](../firmware/README.md) to download the latest combined
image and [the Demo folder](../Demo/README.md) to try The Black Cauldron.
The root `firmware/` folder must contain exactly two files: `README.md` and
`MPE_Firmware-V1.0.12.hex`, the current combined image. Future releases increment
the final version number and replace that one HEX. Keep supporting
documents in `docs/`, and the current download's
[source lock](firmware/source.lock.json) and [checksums](firmware/SHA256SUMS.txt)
in `docs/firmware/`. Versioned kits, manifests, and official restore images
remain in `releases/`. Published release kits and selected GUI snapshots retain
their original bytes.
File paths recorded in the current source lock and checksum ledger resolve
relative to their containing `docs/firmware/` directory.

The current source lock names this consolidated repository. Consolidation itself
preserved the original firmware; later releases record their own source and hashes.

Build native firmware with `scripts/build-firmware.ps1`. It creates a disposable
pinned TeensyROM checkout, applies `engine/patches/`, and incorporates the
selected `gui/` snapshot. The root `Source/` tree remains the desktop/legacy
development tree. Its direct build command does not replace the combined
native build. See [build provenance](BUILD-PROVENANCE.md) for source pins and
[the root README](../README.md) for the current workflow.

AGI-64 remains the separate compiler and cartridge packer. Its firmware import
helper and build wrapper use this consolidated repository; already distributed
kits with the former repository identity remain valid against their original
hashes. Consolidation does not update an installed compiler or flash hardware.

## Consolidation verification

A fresh build from the consolidated tree reproduced native08 byte for byte:
`716bbaa67074da087787f2e4cb912f3a0c35cc3f8e8ff457b7ee75a4dffcdf16`.
Release and selected-source checks passed. The native renderer comparison
covered 73 pictures and overlays, 1,652 VIEW cels, and 132 introduction frames.
The native SD loader passed 5,226 checks with the distributed Black Cauldron
cartridge. The compiler's repository-compatibility checks passed all 21 tests.
These are host/build checks; physical gameplay acceptance is unchanged.
