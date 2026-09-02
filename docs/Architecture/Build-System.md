# Build System

## Combined native MPE release

Use `scripts/build-firmware.ps1` from the repository root to build the current
combined desktop and native MHS Power Engine firmware. Follow the
[root build instructions](../../README.md#build-the-combined-firmware-on-windows)
and [Build Provenance](../BUILD-PROVENANCE.md) for exact input pins and outputs.
This builder applies `engine/patches/` to a pinned upstream checkout, installs
the native engine, and assembles the selected GUI snapshot from `gui/`.
It verifies the generated assets before building both firmware halves.

The `Source/` tree remains available for desktop/backend development. Changes
there must be incorporated into the selected GUI snapshot before the combined
release builder uses them; backend changes require a matching reviewed patch
and policy. Calling
`Source/Teensy/tools/Build-DualBoot.ps1` directly builds that development tree;
it does not apply the complete native release integration.

## Development tree toolchains

The lower-level development build uses two toolchains in a fixed order.
See [Source/BuildInfo.md](../../Source/BuildInfo.md),
[Source/C64/README.md](../../Source/C64/README.md), and
[Source/Teensy/tools/Build-DualBoot.md](../../Source/Teensy/tools/Build-DualBoot.md)
for that workflow.

## Build order (matters)

1. **C64 side first** — `Source/C64/BuildAllC64.bat` (or per-project `build*.bat`) assembles all 6502 sources and copies generated headers into `Source/Teensy/TRMenuFiles/ROMs/`.
2. **Teensy firmware second** — Arduino IDE / arduino-cli build, which embeds those headers as compiled-in byte arrays.

Skipping step 1 after a C64-side change can leave a direct development-tree
build using stale menu/settings/utility headers. The root combined release
builder separately rebuilds and verifies its locked GUI assets.

## C64 side

- Toolchain: ACME cross-assembler 0.97 (all sub-projects except `TRCustomBasicCommands`, which uses KickAssembler + Java JRE 1.8)
- `bin2header.py` (Python 3 — Python 2 is explicitly rejected by the script) converts each `.prg`/`.bin` to a C header
- Tool paths centralized in `Source/C64/SetToolPaths.bat`, edited per-machine
- See [C64-Software.md](C64-Software.md) for the sub-project list

## Teensy side

- Arduino IDE 2.x + Teensyduino, board "Teensy 4.1", Optimize "Faster", CPU Speed "600 MHz", USB Type "Serial + MIDI"
- **Known-bad toolchain version: Teensyduino 1.62.0** — its GCC 15.2.1 bump (from 11.3.1) causes intermittent SD-read stalls with 2 PSRAM chips installed. Current pinned/recommended version is **1.61.0** (as of FW 0.8, 2026-08-02). Root cause confirmed to be the toolchain, not TeensyROM source — do not "fix" this by changing source code.
- Alternative: generate a `.hex` and flash via SD/USB drive instead of direct IDE upload (needed since the Teensy USB power trace is severed during assembly, and TR must be C64-powered to program directly)

## Dual-boot linking (`Build-DualBoot.ps1`)

Produces the combined `TeensyROM(+)_<ver>_full.hex` containing **both** the full firmware and the MinimalBoot image (see [Teensy-Firmware.md](Teensy-Firmware.md#minimalboot-vs-full-firmware) for why MinimalBoot exists). Steps: downloads a version-pinned, SHA256-checked `arduino-cli` if not present, builds TeensyROM Main, builds MinimalBoot, combines both into one hex. Does **not** flash automatically — use Teensy Loader afterward.

Guardrail worth knowing about: before building, the script checks `Fab04FeatureCtl.h` for an active `#define Fab04_Features`. If set but `-Fab04_Features` wasn't passed to the script, it interactively prompts (y/N) to comment out the define and continue as a plain TR build, or abort — this exists specifically to prevent accidentally building a TR+ image mislabeled as plain TR.

<br>

[Back to Architecture Overview](Overview.md)
