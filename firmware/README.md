# Generic GUI / VM host firmware

Download [MPE_Firmware-V1.1.2.hex](MPE_Firmware-V1.1.2.hex) for TeensyROM+
v0.4 / Teensy 4.1, without PSRAM. Copy it to the SD root, install through the
GUI firmware updater, reboot and check V1.1.2 in About.

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
experimental raster modes with cadence/blanking and left-edge tradeoffs.
Physical speed and picture quality still require testing; see the
[V1.1.2 test report](../docs/Architecture/NES-VIDEO-V1.1.2-TEST-STATUS.md).
The previous V1.1.1 fast image remains recoverable from commit b5167fa.

The current build and verification commands are in the [project README](../README.md).
Older immutable firmware kits remain in [releases/](../releases/).
Never mix old native-engine clients with this modular distribution.
