# MHS Power Engine and TeensyROM custom GUI

This repository builds the native MHS Power Engine and its selected C64 menu
together for TeensyROM+ Fab0.4. The AGI-64 Compiler remains a separate project;
it packages games and distributes a matching firmware kit from this release.

The native engine runs original AGI bytecode, parser, motion, pictures, actors,
and sound on Teensy 4.1. The C64 presents acknowledged frames and sound and
supplies keyboard, joystick, and optional 1351 mouse input. Native gameplay
does not emulate a 6510 or require optional PSRAM. Native CRTs launch from SD. Small games keep their existing boot layout;
larger packages use up to 4 MiB with native-only resource banks. The C64
mailbox bank stays reserved. The combined firmware also
retains the earlier MPE services for compatible older cartridges.

## Source and release

- `engine/native-game/`: the nine portable native engine/integration sources.
- `engine/patches/`: the ordered 37-patch TeensyROM integration series.
- `engine/vendor/vrEmu6502/`: byte-exact upstream dependency for the retained
  legacy MPE2 service, with its own license and source pin.
- `engine/custom-gui/`: reviewed GUI backend patch and scope policy.
- `gui/selected-e305/`: the 49 verified GUI source, test, asset-header and
  reference files, with a per-file provenance lock.
- `scripts/`: the standalone firmware builder and GUI validation helper.
- `tests/`: native core, session and firmware checks. Game inputs are supplied
  separately; this repository does not distribute game resource packages.
- `releases/native07/`: the matching native07 custom HEX, official restore HEX,
  user guide, checksums and source manifest.
- `releases/native06/`: the preserved native06 release and source manifest.
- `releases/native05/`: the preserved native05 release and source manifest.

The GUI source is pinned to
`e305f6dc24c526b1e337e9718fbb71d599ed70d8`. Its source is rebuilt and checked
against the committed generated headers before incorporation into the same
dual firmware image. The unrelated current GUI checkout is not a build input.

## Build on Windows

Install Git, Node.js 20.11 or later, PowerShell, and ACME 0.97. The builder
downloads the pinned Arduino CLI 1.4.1, Teensy core 1.61.0, and CRC32 2.0.0
when they are not already in its toolchain directory.

```powershell
.\scripts\build-firmware.ps1 -CustomGuiAcmePath C:\Tools\ACME\acme.exe
```

The default output is `build/native07/`: a disposable pinned upstream checkout
in `source/`, firmware in `firmware/`, and detailed provenance in `manifests/`.
The default toolchain cache is `build/toolchain/`. ACME can also be on `PATH`.

To select another output directory and reuse an installed toolchain:

```powershell
.\scripts\build-firmware.ps1 `
  -ToolchainRoot C:\Tools\TeensyBuild `
  -CustomGuiAcmePath C:\Tools\ACME\acme.exe `
  -OutputRoot .\build\rebuild-native07
```

Use `-SourcePath` only for a disposable checkout at the pinned upstream commit.
The builder verifies the patch chain, GUI source and generated assets, runs
conformance checks, builds both firmware halves, and checks RAM reserves. It
does not flash hardware or update an external compiler checkout.

See [build provenance](docs/BUILD-PROVENANCE.md) and the
[firmware guide](docs/FIRMWARE-GUIDE.md) for the pins and installation process.

## Existing local files

The older root `firmware/`, `patches/`, `backups/`, `diagnostics/`, and handoff
documents predate this repository. They are preserved on disk and ignored by
Git. In particular, the old `0007-geos-desktop.patch` and preview firmware are
not inputs to the current build. Use `engine/` and the matching `releases/` directory.

Upstream notices are retained in [the TeensyROM license](docs/TEENSYROM-LICENSE.md),
the vendored GUI source, and the vrEmu6502 dependency. Provenance does not change
the licenses or ownership of those sources.
