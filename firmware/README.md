# Generic GUI / VM host firmware

Download [MPE_Firmware-V1.1.1.hex](MPE_Firmware-V1.1.1.hex) for TeensyROM+
v0.4 / Teensy 4.1, without PSRAM. Copy it to the SD root, install through the
GUI firmware updater, reboot and check V1.1.1 in About.

Download engines separately from [vms/](../vms/). This firmware has no embedded
AGI, DOS, NES or Doom engine. V1.1.1 adds generic writable storage and a separate
RAM1 support arena / RAM2 guest-memory contract. Install ABI 2 NESVM and/or
DOSVM; only the selected engine is loaded. Replace the V1.1.0 NES client/module
together when upgrading, preserving your private ROMs.

The September 4 fast-test firmware is now the normal main-branch download.
It adds generic VIC cell DMA transport; game engines still live only in their
VM packages. NESVM uses it to avoid the old visible packet-by-packet full-screen
transfer, alongside module-side CPU optimizations. Physical speed and picture
quality still require testing; host tests are not a performance guarantee.

This is byte-for-byte the already-issued fast-test HEX (5,809,940 bytes):

`90dbbce97b5e40b4e77c37902e2407711ef6f36c1421c15c7aaac48d28991a8b`

No reflash is needed if that image is installed. An earlier pre-DMA image also
displayed V1.1.1, so the About version alone does not identify this baseline.
If you installed the older public download rather than the fast-test image,
install this HEX before using the current NESVM package. New indexed-video
modes are not part of this image; they are a subsequent release.

The current build and verification commands are in the [project README](../README.md).
Older immutable firmware kits remain in [releases/](../releases/).
Never mix old native-engine clients with this modular distribution.
