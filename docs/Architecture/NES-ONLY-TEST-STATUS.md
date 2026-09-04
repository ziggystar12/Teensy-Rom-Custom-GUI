# NES-only modular GUI test — V1.1.0 / vm-test-1

Date: September 4, 2026. Baseline being synchronized to the repository's default
branch, `main`. The user reports that SMB launches on physical hardware, but
runs severely slowly (subjective comparison: about 10x slower than the previous
DOS emulator). Ctrl+Commodore+F7 sharp mode improves the image only partially.
This establishes launch evidence, not sustained performance acceptance or a
measured frame rate. NES optimization is deferred until the modular DOS test.
The user can visibly watch blocks of scanlines drawing, unlike the previous
DOS output which appeared to update whole screens. Investigate packet service,
presentation and draw throughput separately from CPU emulation speed; neither
the bottleneck nor a 10x CPU slowdown has been measured.

## Test kit

`build/vm-test/NESVM-TEST-V1.1.0.zip` contains exactly the SD-card contents:

```
MPE_Firmware-V1.1.0.hex
NESVM.crt
VMS/NESVM/manifest.vmi
VMS/NESVM/engine.mvm
VMS/NESVM/client.crt
VMS/NESVM/ROMS/Crossbow.nes
```

Copy these files/folders to the SD root, install the HEX through the GUI's
firmware updater, reboot and confirm **MPE FIRMWARE V1.1.0** in About. Open
`NESVM.crt` for the picker, or select a `.nes` file directly from an SD folder.
Do not use an older NESVM CRT with this firmware. Reboot to return to the GUI;
Start+Select returns to the NES picker only, preserving the selected row.

Only the authorized Crossbow demo is bundled. Add private ROMs yourself; more
than 17 entries are needed to see multiple picker pages. Mapper 0 and mapper 11
NTSC games are supported by the present core. Other mapper/region/save formats
are rejected with a menu message, not claimed as supported. No SMB ROM was
available in the repository. The user's SMB launch report is recorded above;
no private SMB ROM is distributed.

The current software verification and exact file hashes are in
`build/vm-test/verification.json`; detailed output is in `verification.log`.
`build-inputs.json` freezes the source inputs and verification rejects drift.

If a hardware test fails, photograph the whole diagnostic screen and note the
game filename, PAL/NTSC mode and whether it was launched directly or through
the picker. The new host's latched FB codes are hexadecimal: `11` invalid MVM
header/profile, `12` module read failure, `13` module checksum failure, `14`
module initialization/export rejection, and `15` invalid module packet.

## Steps 1–6: implemented portion and outstanding gates

| Step | Implemented for this test | Not claimed complete |
| --- | --- | --- |
| 1: baseline | Source baseline `18bf8c31bbc05e8d322cf5327f5ef8a07b4468b2`, photographed register records below, retained old release artifacts, exact new kit hashes. | Exact firmware/client hashes actually installed in the user's C64. |
| 2: regressions | Corrected full-replace menu updates, scene changes during pending ACKs, a missing hash-label terminator and falsely asserted SID frame-end flags. The client now captures the exact status byte that triggered a firmware error. | Proof these defects caused the photographed Crossbow/SMB/DOS failures; physical long-run stress. |
| 3: ABI/SDK | Fixed-width MVM1 image, version/service checks, trusted-local native modules, generic file/read-at/list/clock services, immutable packet/ACK interface, local C64 SDK. | Future serial and other service additions. |
| 4: host/memory | Separate reset-time host profile, bounded ITCM loader, MPU load/execute protection, full module-owned RAM2, DTCM host heap, linker/flash partition checks, separate diagnostic module. | Physical FlexRAM/MPU execution, diagnostic RAM fill/read, power/reset testing, 20 alternating launches and 100,000-packet/30-minute stress. Flash/XIP module caching is deferred. |
| 5: discovery/launch | Bounded `/VMS` manifests, protected/duplicate extension rejection, generic browser VM type, exact selected content, package/client CRC preflight, one-shot reset request and picker launch. | SD hot-removal and updater/reset testing on the actual board. |
| 6: NES extraction | No engines linked into firmware. CPU/PPU/mapper/renderer/SID/picker live in `engine.mvm`; row/page navigation, complete-frame fitting and ACK-safe scene changes are tested. | Physical Crossbow and SMB playability, sustained frame rate and audio/input feel. |

This deliberately does **not** mark every original step exit gate as passed.
It provides the requested fresh GUI/NES architecture for the next physical test.
AGI, DOS and Doom are not bundled, enabled, or used as compatibility fallbacks.

## Measured memory model

The module's 75,300 bytes of executable code/constants load into the reserved
128 KiB RAM1 ITCM module window, starting at `0x00020000`. The host occupies the
lower ITCM window. The reset-time FlexRAM split is 256 KiB ITCM / 256 KiB DTCM.

RAM2 is exactly 524,288 bytes: 3,168 bytes of NES module static data/BSS and
521,120 bytes of NES workspace. That workspace includes the picker, machine,
renderer, frozen/presented frames and ROM storage; it is not all guest RAM.
No executable code, host heap, USB buffer or PSRAM allocation is placed there.

The host's heap is a separate 32 KiB DTCM reservation. Link symbols show a
162,784-byte host stack budget after that heap. The ordinary GUI has a different
memory map; its linked stack budget is 30,492 bytes. The Teensy size utility's
default free-RAM summary does **not** account for the custom fixed-bank host
profile; use the linker-symbol checks, not that default summary.

## Automated evidence

- Actual NES module: 40-ROM picker, 17-row page changes and clamped edges; a
  single-row movement publishes 77 changed cells, with no replacement/blanking.
- Pending frame and packet contents remain unchanged when launch input arrives.
- Direct selection runs the exact requested file even outside the picker list.
- Crossbow ran for 120 presented frames through module callbacks and ACKs in
  the host harness; the portable core passed 164,369 checks.
- MVM1 header corruption, truncation, overflow, ABI/service/address checks and
  payload CRC rejection pass; registry tests cover launch/preflight failures.
- Actual C64 client reset, START, bounded missing-host timeout and SID receiver
  pass in VICE for PAL and NTSC. This is not a Teensy bus emulation test.
- 30 focused assembled GUI browser, launch, firmware-dialog and settings tests
  pass. GUI assets are reassembled from the current source for every build.
- Both firmware ELFs contain no AGI, DOS, NES or Doom engine symbols. Host RAM2
  and external-RAM static sections are empty; ISR/IO2 handlers remain in ITCM.
- A separate engine-free diagnostic MVM compiles with the same ABI. It is not
  installed in the NES-only SD kit and has not run on physical hardware.

## Recorded field evidence, not diagnosed causes

DOS photo: R23, stage 03, error 03, packets 0000; controls F0–F7:
`4D 33 54 50 01 E0 00 01`; F8–FF: `00 40 00 04 01 00 01 1C`.
Firmware detail `04` identifies a memory/startup error class, not one unique
root cause. DOS is excluded from this test, not silently declared repaired.

Crossbow photo: stage 05, error 03, packets 12CC; controls F0–F7:
`4D 33 54 50 03 03 E0 E0`; F8–FF: `00 01 00 00 63 81 69 4C`.
The last packet is SID type 02, sequence E0. The later status snapshot is 03,
not the >=E0 value that would have triggered the old error branch. The new
client records the triggering status so a repeat failure is more informative.

SMB: the user observed the first screen followed by a crash; no register record
or exact ROM identity is available. Architecture alone is not proof of a fix.

## Repository cleanup

The 22 `gui/selected-*` directories were removed as requested. They were clean
and are recoverable from Git history. `Source/` is now the single GUI input
tree. `scripts/build-firmware.ps1` invokes the new builder; the old 50-patch
engine-in-firmware path is no longer the current build. Existing releases and
unrelated `apple2/` and `c128/` work were left alone. This baseline is now being
committed and synchronized at the user's request; no hardware flash is performed
by the agent.

The retained V1.0.21 reference firmware has SHA-256
`4261dcd708872c46f6e50547065bb91c98347807538445894c794d1da6af3c76`.
This identifies the repository rollback file, not the user's installed image.
