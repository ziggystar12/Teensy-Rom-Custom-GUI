# TeensyROM Custom GUI — modular VMs

Current development build: **V1.1.5 / vm-test-6**, for TeensyROM+ v0.4 and
Teensy 4.1 with 1 MiB internal RAM. No PSRAM is required.

The GUI firmware now contains a generic VM host, not the AGI, DOS, NES or Doom
engines. NESVM, DOSVM and AGIVM are independently loaded packages. Only one runs
at a time; all reuse the same RAM1 code/support arena and RAM2 guest memory.
Doom is not ported yet. No legacy built-in VM compatibility is retained.

## Try it

Download the [firmware](firmware/) and the [VM packages](vms/) you want.
VMs are separately downloadable under [vms/](vms/); future packages go there too.
Extract the VM ZIP to the SD root and copy the firmware HEX there:

- `MPE_Firmware-V1.1.5.hex` — combined GUI and generic VM host firmware.
- `NESVM.crt` — C64 launcher/client.
- `VMS/NESVM/` — manifest, independently compiled engine, client and ROM folder.
- `DOSVM.crt` and `VMS/DOSVM/` — separate DOS module, BIOS, C: image and D: folder.
- `AGIVM.crt` and `VMS/AGIVM/` — separate AGI engine and `.AGI` game picker.

Install the HEX through the firmware updater, reboot and confirm V1.1.5 in
About. Open NESVM.crt to choose a ROM, or browse directly to a .nes file on SD.
V1.1.5 removes steady F3/F5 blanking with inactive-bank border uploads, centers
F5 and F7, moves hot NES RAM to RAM1, and adds a measured speed readout on the
picker. Update BOTH firmware and NESVM.zip, including from V1.1.4. The picker
and emulation-first scheduling fixes are retained. See the
[release notes and physical test](docs/Architecture/NES-VIDEO-V1.1.5.md).
The supplied Crossbow demo is the only bundled game. Private ROMs stay private.
Left/Right pages through 17 rows; Up/Down changes the highlight without blanking.
Reboot the C64/Teensy to return to the GUI. Do not mix this kit with older clients.
V1.1.2 adds an indexed video service, so replace the NES client/module together
and update firmware even from the V1.1.1 fast-test kit. Preserve your ROMs. Follow the [DOS instructions](vms/DOSVM/)
to preserve existing disks and saves; never extract a fresh C: image over yours.

The user confirmed that **SMB launches on the V1.1.0 baseline**, but reports
severe slowdown (subjectively about ten times slower than the previous DOS VM).
V1.1.2 includes the merged fast DMA firmware, optimized NES core and repaired
keyboard/joystick picker. NES now submits native pixels to the MPE firmware,
which owns video conversion. Hold Commodore+Control and select unshifted F1
(Default/wide-pixel), F3 (Auto-8), F5 (Enhanced-25), or F7 (Sharp). These are
direct choices, not toggles. No per-game analysis or settings are needed.
F5 and F7 center the 256 NES columns within the 320-wide hires canvas;
F1 remains the startup default. V1.1.4 still ran at roughly one-third speed on
hardware. The new speed readout must be checked; full-speed play is not claimed.

Default/Sharp retain fast steady-frame DMA. Enhanced modes have lower picture
cadence and a left-edge FLI artifact; they are experimental. Host/module
and PAL/NTSC raster/bitmap tests pass, but physical playability and sustained
cadence remain open. See the [video test report](docs/Architecture/NES-VIDEO-V1.1.2-TEST-STATUS.md)
and [picker fix](docs/Architecture/NES-PICKER-INPUT-FIX.md).
See the [modular DOS test report](docs/Architecture/DOS-MODULAR-TEST-STATUS.md).
Nothing is flashed or publicly published by the build scripts.

AGIVM retains its native video solution and existing package. It works with
the generic V1.1.1-or-newer host; the new selectors do not apply to AGI.
Extract [AGIVM.zip](vms/AGIVM.zip), then select `AGIVM.crt` or a compiled `.AGI`
file directly. See [AGI setup and content compilation](vms/AGIVM/README.md).
KQ1/SQ1 module and C64-client tests pass; physical gameplay acceptance is open.

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

AGI-only build (does not rebuild firmware or NES/DOS):

```powershell
node scripts/build-agivm.mjs
node scripts/verify-agivm.mjs
node scripts/publish-agivm.mjs
```

## Source layout

| Path | Purpose |
| --- | --- |
| `Source/` | Single maintained GUI and generic firmware source tree. |
| `vm/abi/` | Module ABI documentation and linker profile. |
| `vm/client/` | Local shared C64 client SDK; no AGI-64 checkout dependency. |
| `vm/nes/` | NES module, picker and module-side adapters. |
| `vm/dos/` | DOS module, compact PC hardware backing and file adapter. |
| `vm/agi/`, `agi/` | AGI module, content compiler bridge and focused tests. |
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
