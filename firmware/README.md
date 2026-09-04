# Generic GUI / VM host firmware

Download [MPE_Firmware-V1.1.1.hex](MPE_Firmware-V1.1.1.hex) for TeensyROM+
v0.4 / Teensy 4.1, without PSRAM. Copy it to the SD root, install through the
GUI firmware updater, reboot and check V1.1.1 in About.

Download engines separately from [vms/](../vms/). This firmware has no embedded
AGI, DOS, NES or Doom engine. V1.1.1 adds generic writable storage and a separate
RAM1 support arena / RAM2 guest-memory contract. Install ABI 2 NESVM and/or
DOSVM; only the selected engine is loaded. Replace the V1.1.0 NES client/module
together when upgrading, preserving your private ROMs.

The earlier NES baseline launched SMB but ran severely slowly. No NES speed fix
is claimed here. DOS is ready for physical startup, Tandy and speed comparison;
host tests are not a physical performance guarantee.

The current build and verification commands are in the [project README](../README.md).
Older immutable firmware kits remain in [releases/](../releases/).
Never mix old native-engine clients with this modular distribution.
