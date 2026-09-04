# VM ABI 2 — reset-only NES/DOS platform

The firmware owns hardware, reset, SD file handles, a clock and immutable
CRC-protected C64 packets. It contains no emulator. `engine.mvm` supplies all
game/emulator, menu, presentation and input policy code. The same host loads
NESVM or DOSVM without identifying either engine in firmware.

Install `/VMS/<id>/manifest.vmi`, `engine.mvm`, `client.crt` and support files.
The manifest is six ASCII lines: `VM1`, package ID, associated extension
(without dot), module filename, client filename, then `END`. Names are bounded
and path components cannot traverse outside the package. Only SD launch is
supported in this pilot. The registry scans at most 32 installed packages;
duplicate extensions are rejected. No list of emulator IDs is compiled in.

Launching a registered content file or package client records a bounded
one-shot launch request and resets the Teensy. MinimalBoot consumes it before
releasing C64 reset. Subsequent reset returns to the GUI; no unload is supported.

The pilot uses reset-time FlexRAM setup: 192 KiB ITCM and 320 KiB DTCM.
Only one selected module is loaded; NES and DOS reuse the same addresses.

| Region | Reservation |
| --- | --- |
| RAM1 ITCM `0x00000000..0x00017fff` | Generic host code (96 KiB ceiling) |
| RAM1 ITCM `0x00018000..0x0002ffff` | Selected VM code/constants (96 KiB) |
| RAM1 DTCM below `0x20014000` | Host state, 16 KiB heap and alignment gap |
| RAM1 DTCM `0x20014000..0x20043fff` | VM data/BSS and remaining support workspace (192 KiB total) |
| RAM1 DTCM `0x20044000..0x2004ffff` | Shared execution/interrupt stack (48 KiB) |
| RAM2 `0x20200000..0x2027ffff` | Guest machine RAM only (512 KiB); no host or VM support allocations |

NES's CPU/PPU RAM, CHR RAM and loaded cartridge use RAM2; its machine controls,
menus and renderer use RAM1. DOS exposes the entire 512 KiB as conventional
guest RAM and places BIOS/peripheral/video/console support in RAM1.
MinimalBoot disables USB and uses FIFO SDIO. Link checks enforce all boundaries.
The GUI has its own link map, discarded on VM launch. This is not live FlexRAM
repartitioning and does not require both engines to fit at once.

The 64-byte image declares bounded text/data/BSS, entry point, ABI and services.
`ram_base` describes the RAM1 module-data base, not guest RAM. `workspace`
follows initialized data and zeroed BSS; `guest_ram` independently supplies all
RAM2. Both clients and modules must use ABI 2, paired with firmware V1.1.1.
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

ABI 2 adds generic read/write/create/truncate/flush/close, directory operations,
timestamps and allocation-unit-aware free-space queries, with 24 inline file
handles. There is no DOS filesystem policy in firmware. DOS implements its
8.3 paths, sharing rules, C: block device and D: redirector inside the module.
`should_yield` supports bounded foreground work on input/ACK/retry/time limits.
Quiet retry command 4 stops foreground module work until the pending packet's
ACK; the packet stays immutable. This uses only the existing IO2 dispatch hook,
not added branches in `isrPHI2`.
