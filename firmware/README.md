# Generic GUI / VM host firmware

Download [MPE_Firmware-V1.1.0.hex](MPE_Firmware-V1.1.0.hex) for TeensyROM+
v0.4 / Teensy 4.1, without PSRAM. Copy it to the SD root, install through the
GUI firmware updater, reboot and check V1.1.0 in About.

Download engines separately from [vms/](../vms/). This firmware has no embedded
AGI, DOS, NES or Doom engine. V1.1.0 provides the initial generic modular host;
NES is its first module. SMB launch is user-confirmed, but NES runs severely
slowly and needs substantial performance work.

The current build and verification commands are in the [project README](../README.md).
Older immutable firmware kits remain in [releases/](../releases/).
Never mix old native-engine clients with this modular distribution.
