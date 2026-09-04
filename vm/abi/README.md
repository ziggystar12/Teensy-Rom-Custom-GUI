# VM ABI 1 — NES-only test platform

The firmware owns hardware, reset, SD file handles, a clock and immutable
CRC-protected C64 packets. It contains no emulator. `engine.mvm` supplies all
NES CPU/PPU/APU, mapper, menu, presentation and input policy code.

Install `/VMS/<id>/manifest.vmi`, `engine.mvm`, `client.crt` and support files.
The manifest is six ASCII lines: `VM1`, package ID, associated extension
(without dot), module filename, client filename, then `END`. Names are bounded
and path components cannot traverse outside the package. Only SD launch is
supported in this pilot. The registry scans at most 32 installed packages;
duplicate extensions are rejected. No list of emulator IDs is compiled in.

Launching a registered content file or package client records a bounded
one-shot launch request and resets the Teensy. MinimalBoot consumes it before
releasing C64 reset. Subsequent reset returns to the GUI; no unload is supported.

The pilot uses the core's reset-time FlexRAM setup: 256 KiB ITCM and 256 KiB
DTCM. Host code occupies ITCM below 128 KiB; an independently linked MVM1
module occupies the upper 128 KiB. RAM2's entire 512 KiB belongs to module data,
BSS and workspace. Host heap and live state are in DTCM. MinimalBoot disables
USB and uses FIFO SDIO. Link-map checks enforce these reservations and a 48 KiB
minimum host stack. Full GUI and module-host images have different link maps.

The image declares bounded text/data/BSS, entry point, ABI and capabilities.
Both header and loaded payload CRC32 must match before code executes. CRC
detects corruption, not hostile code: modules are trusted-local native code.
Only one module is loaded per reset. No PSRAM, module flash cache, firmware
rewriting, constructors, global heap allocation or binary compatibility with
the retired built-in engines is required. Flash/XIP profiles for larger future
VMs remain a separate hardware-validated feature; this pilot cannot claim them.

The host may call `pump` while a packet awaits ACK. Module output and its
associated frame stay frozen until `ack`; menu/game transitions wait until the
old frame completes. Firmware-owned status/commit/error registers are never
client-writable. A fatal error remains latched until reset.
