# TeensyROM Architecture Overview

Reference documentation for AI assistants and contributors ramping up on this codebase. Dense and structural by design — see linked docs for user-facing feature explanations, and [Constraints.md](Constraints.md) for hard rules not to violate.

The consolidated project contains both the desktop development tree and the
native MHS Power Engine release inputs. Start with the
[root README](../../README.md) for downloads and the Black Cauldron demo.
The authoritative combined release build is `scripts/build-firmware.ps1`;
[Build Provenance](../BUILD-PROVENANCE.md) identifies its locked GUI snapshot,
engine sources, patch chain, and firmware manifests. The upstream architecture
and lower-level `Source/` build described below remain useful for development.

TeensyROM is a Teensy 4.1-based multi-function cartridge for the Commodore 64/128: ROM (CRT) emulator, instant PRG loader, MIDI/SID interface, Ethernet/BBS bridge, NFC launcher, and remote-control target. Two hardware variants share one firmware codebase: **TR** (original, PCB v0.3, DMA-line-assert only) and **TR+** (PCB v0.4, adds true bus-mastering DMA enabling Kernal Replacement, a real 512KB REU, freezer cartridge emulation, and remote DMA memory access). TR+ is gated in firmware by `Fab04_Features` in `Source/Teensy/MinimalBoot/Common/Fab04FeatureCtl.h`.

## Repo layout

| Path | Contents |
|---|---|
| `Source/Teensy/` | Teensy microcontroller firmware — C/C++, Arduino/Teensyduino build. See [Teensy-Firmware.md](Teensy-Firmware.md). |
| `Source/C64/` | Programs that run ON the C64 (menu, settings, utilities) — 6502 assembly. See [C64-Software.md](C64-Software.md). |
| `Source/BuildInfo.md` | Lower-level development build instructions for the `Source/` tree. |
| `engine/` | Native AGI engine, ordered integration patches, reviewed GUI backend policy, and licensed legacy dependency. |
| `gui/` | Selected GUI snapshots and provenance locks consumed by the combined release builder. |
| `scripts/build-firmware.ps1` | Authoritative combined native MPE and desktop firmware builder. |
| `firmware/` | Only the current combined firmware image and its README. |
| `docs/firmware/` | Current download checksums and source lock. |
| `releases/` | Immutable versioned firmware kits, restore images, checksums, and source manifests. |
| `Demo/` | Ready-to-play Black Cauldron CRT with instructions and credits. |
| `docs/` | Usage, build, native engine, design, and historical validation documentation. |
| `docs/Architecture/` | This doc set — structural/AI-reference material, not user-facing. |

## The two toolchains, and how they connect

The `Source/` development tree is built in two passes that feed into each
other in one direction only. The combined native release builder performs
these steps against its pinned inputs in a disposable build tree:

1. **C64 side builds first.** 6502 assembly sources under `Source/C64/*/source/` are cross-assembled (ACME, or KickAssembler for `TRCustomBasicCommands`) into raw binaries, then converted by `bin2header.py` into C headers (`static const unsigned char ..._prg[]`) and copied into `Source/Teensy/TRMenuFiles/ROMs/`.
2. **Teensy firmware builds second**, embedding those generated headers directly as byte arrays — the on-screen menu, settings pages, and bundled utility programs are compiled-in C64 machine code, not generated at runtime.

This means a change to any C64-side `.asm` requires rebuilding that sub-project (or running `BuildAllC64.bat`) *before* rebuilding the Teensy firmware, or the change won't be picked up. Full details: [Build-System.md](Build-System.md).

For native releases, desktop changes must also be reviewed and incorporated
into the selected `gui/` snapshot. Backend changes require the matching
`engine/custom-gui/` patch and policy. Editing the development `Source/` tree
alone does not change the locked release inputs.

C64 code and Teensy firmware also talk to each other **at runtime** two different ways — a low-level memory-mapped cartridge register protocol, and (separately) an external host-facing USB/Ethernet protocol. See [Comms-Protocol.md](Comms-Protocol.md).

## Doc set

- [Native firmware guide](../FIRMWARE-GUIDE.md) — combined firmware installation, native cartridges, saves, and recovery
- [Build Provenance](../BUILD-PROVENANCE.md) — native08 build inputs and preserved release records
- [Legacy acceleration](GENERIC-ACCELERATION.md) — retained AGI+3/PowerVM services and their C64 fallback boundary

- [Teensy-Firmware.md](Teensy-Firmware.md) — module map, entry point, IO_Handlers pattern, MinimalBoot
- [C64-Software.md](C64-Software.md) — MainMenuCRT, SettingsMenu, sub-programs, build pipeline
- [Comms-Protocol.md](Comms-Protocol.md) — cartridge register protocol + link to external host protocol
- [Build-System.md](Build-System.md) — toolchains, versions, dual-boot linking, known gotchas
- [Constraints.md](Constraints.md) — hard rules: ISR hot path, memory budgets, toolchain pins
- [Known-Issues.md](Known-Issues.md) — scoped, deferred findings from architecture walkthroughs, with designed (not yet implemented) fixes

<br>

[Back to main ReadMe](/README.md)
