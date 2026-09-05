# Generic GUI / VM host firmware

Download [MPE_Firmware-V1.1.7.hex](MPE_Firmware-V1.1.7.hex) for TeensyROM+
v0.4 / Teensy 4.1, without PSRAM. Copy it to the SD root, install through the
GUI firmware updater, reboot and check V1.1.7 in About.

V1.1.7 combines DoomVM's RAM2_RO96 loader with the NES/GB enhanced-video
updates and DOSVM's shared graphics service. Install the new
[DOSVM-update.zip](../vms/DOSVM-update.zip) to update DOS without overwriting
either drive, and the matched [NESVM.zip](../vms/NESVM.zip). Existing GBVM and
AGIVM packages remain compatible; AGI's video is unchanged. Doom's game-data
kit remains a local, separately licensed test artifact, not embedded firmware.
See [V1.1.7 integration notes](../docs/Architecture/VM-VIDEO-V1.1.7.md).

**September 5 DOS correction:** the original Tandy/CGA renderer and changed-cell
transport remain default after black/static-screen regressions. F5 explicitly
opts into shared Enhanced-25; F1 returns to the original renderer. Keep V1.1.7
installed; no reflash or NES/GB update is needed. [Details](../docs/Architecture/DOS-F5-OPT-IN.md).

## Historical changes

V1.1.5 replaces per-frame F3/F5 blanking with inactive-bank border uploads and
centers F5's 256-wide NES image. Install the matching [NESVM.zip](../vms/NESVM.zip),
including the root launcher, client and engine. The engine also puts hot NES
RAM in RAM1 and reports measured speed when returning to the ROM picker.
The V1.1.4 hardware report remained about one-third speed; full-speed playback
is NOT claimed fixed. See [V1.1.5 changes and test steps](../docs/Architecture/NES-VIDEO-V1.1.5.md).

V1.1.4 fixes emulation starvation caused by zero-budget ACK turns. Install the
updated [NESVM.zip](../vms/NESVM.zip) as well: its engine now prioritizes emulated
time over new video transfers, preserves elapsed time and avoids wasted sound
packets/pixel composition. Centered native-width F7 and the picker fix are
included. See the [timing regression](../docs/Architecture/NES-TIMING-V1.1.4.md).
The regression passes; full-speed gameplay/music still require a hardware rerun.

Download engines separately from [vms/](../vms/). This firmware has no embedded
AGI, DOS, NES or Doom engine. V1.1.1 adds generic writable storage and a separate
RAM1 support arena / RAM2 guest-memory contract. Install ABI 2 NESVM and/or
DOSVM; only the selected engine is loaded. Replace the V1.1.0 NES client/module
together when upgrading, preserving your private ROMs.

V1.1.2 builds directly on the merged fast-DMA host. It adds generic indexed
video conversion and Default/Auto-8/Enhanced-25/Sharp selection, opted into by
NESVM first. Install the matching current NES package, which also includes the
optimized NES core and repaired keyboard/joystick picker. Existing DOS and AGI
packages remain unchanged; AGI does not use the new video modes.

This is a NEW firmware image: update even if the September 4 V1.1.1 fast-test
HEX is installed. Default/Sharp retain fast steady-frame DMA. F3/F5 are
experimental raster modes with cadence and left-edge tradeoffs. Their V1.1.5
steady-frame path no longer intentionally clears the display-enable bit.
Physical speed and picture quality still require testing; see the
[V1.1.2 test report](../docs/Architecture/NES-VIDEO-V1.1.2-TEST-STATUS.md).
The previous V1.1.1 fast image remains recoverable from commit b5167fa;
the V1.1.2 image remains in commit 7ee88e7.
The V1.1.3 image remains recoverable from commit c92918a.
The V1.1.4 image remains recoverable from commit cc14f28.

The current build and verification commands are in the [project README](../README.md).
Older immutable firmware kits remain in [releases/](../releases/).
Never mix old native-engine clients with this modular distribution.
