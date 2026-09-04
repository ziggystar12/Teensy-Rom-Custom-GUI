# TeensyROM Custom GUI — modular VMs

Current development build: **V1.1.1 / vm-test-2**, for TeensyROM+ v0.4 and
Teensy 4.1 with 1 MiB internal RAM. No PSRAM is required.

The GUI firmware now contains a generic VM host, not the AGI, DOS, NES or Doom
engines. NESVM and DOSVM are independently loaded packages. Only one runs at a
time; both reuse the same RAM1 code/support arena and RAM2 guest memory. AGI and
Doom are not ported yet. No legacy built-in VM compatibility is retained.

## Try it

Download the [firmware](firmware/) and the [VM packages](vms/) you want.
VMs are separately downloadable under [vms/](vms/); future packages go there too.
Extract the VM ZIP to the SD root and copy the firmware HEX there:

- `MPE_Firmware-V1.1.1.hex` — combined GUI and generic VM host firmware.
- `NESVM.crt` — C64 launcher/client.
- `VMS/NESVM/` — manifest, independently compiled engine, client and ROM folder.
- `DOSVM.crt` and `VMS/DOSVM/` — separate DOS module, BIOS, C: image and D: folder.

Install the HEX through the firmware updater, reboot and confirm V1.1.1 in
About. Open NESVM.crt to choose a ROM, or browse directly to a .nes file on SD.
The supplied Crossbow demo is the only bundled game. Private ROMs stay private.
Left/Right pages through 17 rows; Up/Down changes the highlight without blanking.
Reboot the C64/Teensy to return to the GUI. Do not mix this kit with older clients.
ABI 2 changes the memory contract, so replace the NES client/module as well when
updating from V1.1.0. Preserve your ROMs. Follow the [DOS instructions](vms/DOSVM/)
to preserve existing disks and saves; never extract a fresh C: image over yours.

The user confirmed that **SMB launches on the V1.1.0 baseline**, but reports
severe slowdown (subjectively about ten times slower than the previous DOS VM).
Sharp mode helps the picture only partially. NES performance and visual quality
remain open; this is not a performance-ready release. See
[test status](docs/Architecture/NES-ONLY-TEST-STATUS.md).
The ABI 2 DOS/NES memory layout and DOS speed comparison await physical testing.
See the [modular DOS test report](docs/Architecture/DOS-MODULAR-TEST-STATUS.md).
Nothing is flashed or publicly published by the build scripts.

## Build

On Windows, use the existing pinned tool cache in `build/toolchain/` (Arduino
CLI 1.4.1, Teensy core 1.61.0/GCC 11.3.1, CRC32 2.0.0 and ACME 0.97), plus Node.js:

```powershell
.\scripts\build-firmware.ps1
node scripts/verify-vm-test.mjs
```

The builder copies the toolchain into an isolated build area, assembles the
current C64 GUI, compiles both firmware halves and the external NES/DOS modules,
and checks linker/flash boundaries. It does not alter the shared toolchain.

## Source layout

| Path | Purpose |
| --- | --- |
| `Source/` | Single maintained GUI and generic firmware source tree. |
| `vm/abi/` | Module ABI documentation and linker profile. |
| `vm/client/` | Local shared C64 client SDK; no AGI-64 checkout dependency. |
| `vm/nes/` | NES module, picker and module-side adapters. |
| `vm/dos/` | DOS module, compact PC hardware backing and file adapter. |
| `engine/native-nes/` | Portable NES core used by the independent module. |
| `vm/tests/` | Actual module and image validation tests. |
| `scripts/build-vm-test.mjs` | Matched generic firmware and independent NES/DOS builder. |
| `docs/Architecture/VM-MODULARIZATION-PLAN.md` | Prioritized plan and revised test scope. |
| `engine/`, `dos/`, `doom/` | Portable engines, support assets and historical adapters; no VM engine is linked into firmware. |
| `firmware/` | Current downloadable generic GUI/host firmware. |
| `vms/` | Separately downloadable VM packages and support files. |
| `releases/` | Historical release artifacts. |

Old `gui/selected-*` snapshots have been removed. Source history remains in Git;
there is no snapshot-selection or 50-patch engine integration step in this build.

Hardware and original firmware: [SensoriumEmbedded TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM).
Original GUI project licensing and third-party source notices remain with their files.
