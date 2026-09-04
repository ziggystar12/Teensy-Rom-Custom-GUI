# TeensyROM Custom GUI — modular NES test

Current development build: **V1.1.0 / vm-test-1**, for TeensyROM+ v0.4 and
Teensy 4.1 with 1 MiB internal RAM. No PSRAM is required.

The GUI firmware now contains a generic VM host, not the AGI, DOS, NES or Doom
engines. NESVM is the first independently loaded package. AGI, DOS and Doom are
not supported by this test firmware. No legacy VM compatibility is retained.

## Try it

Download the [firmware](firmware/) and the [NESVM ZIP](vms/NESVM.zip).
VMs are separately downloadable under [vms/](vms/); future packages go there too.
Extract the VM ZIP to the SD root and copy the firmware HEX there:

- `MPE_Firmware-V1.1.0.hex` — combined GUI and generic VM host firmware.
- `NESVM.crt` — C64 launcher/client.
- `VMS/NESVM/` — manifest, independently compiled engine, client and ROM folder.

Install the HEX through the firmware updater, reboot and confirm V1.1.0 in
About. Open NESVM.crt to choose a ROM, or browse directly to a .nes file on SD.
The supplied Crossbow demo is the only bundled game. Private ROMs stay private.
Left/Right pages through 17 rows; Up/Down changes the highlight without blanking.
Reboot the C64/Teensy to return to the GUI. Do not mix this kit with older clients.

The user confirmed that **SMB launches on physical hardware**, but reports
severe slowdown (subjectively about ten times slower than the previous DOS VM).
Sharp mode helps the picture only partially. NES performance and visual quality
remain open; this is not a performance-ready release. See
[test status](docs/Architecture/NES-ONLY-TEST-STATUS.md).
Nothing is flashed or publicly published by the build scripts.

## Build

On Windows, use the existing pinned tool cache in `build/toolchain/` (Arduino
CLI 1.4.1, Teensy core 1.61.0/GCC 11.3.1, CRC32 2.0.0 and ACME 0.97), plus Node.js:

```powershell
.\scripts\build-firmware.ps1
node scripts/verify-vm-test.mjs
```

The builder copies the toolchain into an isolated build area, assembles the
current C64 GUI, compiles both firmware halves and the external NES module,
and checks linker/flash boundaries. It does not alter the shared toolchain.

## Source layout

| Path | Purpose |
| --- | --- |
| `Source/` | Single maintained GUI and generic firmware source tree. |
| `vm/abi/` | Module ABI documentation and linker profile. |
| `vm/client/` | Local shared C64 client SDK; no AGI-64 checkout dependency. |
| `vm/nes/` | NES module, picker and module-side adapters. |
| `engine/native-nes/` | Portable NES core used by the independent module. |
| `vm/tests/` | Actual module and image validation tests. |
| `scripts/build-vm-test.mjs` | Fresh NES-only builder. |
| `docs/Architecture/VM-MODULARIZATION-PLAN.md` | Prioritized plan and revised test scope. |
| `engine/`, `dos/`, `doom/` | Remaining sources for later module work; not linked into this firmware. |
| `firmware/` | Current downloadable generic GUI/host firmware. |
| `vms/` | Separately downloadable VM packages and support files. |
| `releases/` | Historical release artifacts. |

Old `gui/selected-*` snapshots have been removed. Source history remains in Git;
there is no snapshot-selection or 50-patch engine integration step in this build.

Hardware and original firmware: [SensoriumEmbedded TeensyROM](https://github.com/SensoriumEmbedded/TeensyROM).
Original GUI project licensing and third-party source notices remain with their files.
