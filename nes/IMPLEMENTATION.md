# NESVM R1 implementation status

Status date: 2026-09-04. The native Teensy service, ROM picker, generic C64
terminal, basic SID output, and Mapper 0/11 core are integrated in a local
firmware candidate. Host, source, linker, firmware, CRT, package, and VICE
checks pass. Physical TeensyROM+C64/C128 gameplay and audible SID quality still
require operator acceptance; a successful build is not that proof.

## User workflow

1. Flash the matching local NESVM firmware candidate.
2. Copy the contents of `nes/build/package/sd-card` to the root of the SD card.
   This creates root `NESVM.CRT`, `/NESVM/ROMS`, and `/NESVM/SAVES`.
3. Launch `NESVM.CRT` from the ordinary TeensyROM browser.
4. NESVM displays a sorted, paged list of `.nes` files in `/NESVM/ROMS`.
   Use joystick port 2 Up/Down to highlight a game; press port-2 Fire or Return
   to launch it.
5. While playing, press Start+Select (Return+Shift) to return to the list. The
   same ROM remains highlighted so another game can be selected immediately.

Gameplay controls are port-2 directions and Fire=A, Space=B, Return=Start, and
either Shift=Select. Ctrl+Commodore+F7 toggles the display. Sharp Text is on by
default and fits the complete 256x240 NES picture into 320x200; the alternate
160x200 multicolor view is also a complete-frame fit, never a crop.

## Implemented and checked

- Strict bounded iNES/NES 2.0 inspection with clear unsupported reasons.
- Mapper 0 NROM-128/NROM-256 and the discrete bus-conflict Mapper 11 profile
  required by the authorized Crossbow demo.
- Cycle-stepped RP2A03 CPU, bus, controller, interrupt, and OAM-DMA behavior.
- NTSC PPU timing, scrolling, background/sprite composition, mirroring,
  palette RAM, clipping, priority, sprite-zero hit, and whole-frame capture.
- Deterministic Sharp Text and multicolor squish renderers checked against a
  separate host reference converter.
- A basic NES-to-SID adapter: pulse 1/2 use SID voices 1/2; triangle/noise
  share voice 3. It emits the existing 26-byte SID register/retrigger packet.
- Exact `MHS NESVM` and `N6D1` firmware routing. The native service validates
  the terminal identity, lists `/NESVM/ROMS`, reopens the chosen file, verifies
  its size and SHA-256, and compares the loaded bytes against the file hash.
- A 128-entry, 17-row-per-page, case-insensitive `.nes` picker with bounded
  names, visible load errors, and retained selection on return from a game.
- Emulator time continues while a C64 packet ACK is pending; completed visual
  state is coalesced rather than stalling the emulated CPU/APU clock.
- NESVM uses only the unused RAM1 tail behind its fully resident CRT. RAM2 is
  untouched and remains the 512 KiB system/DOSVM region.
- A reproducible 24,688-byte `NESVM.CRT` and SD tree containing exactly the
  explicitly authorized Crossbow demo, plus the empty future `SAVES` folder.
- Private `nes/ROMS`, work, builds, reports, and captures remain ignored. Only
  the exact Crossbow hash in `DEMO/README.md` is eligible for distribution.

The synthetic suite passes 164,369 checks. The authorized 98,320-byte Mapper-11
Crossbow file completed a 300-frame host run with 8,931,879 CPU cycles, 296 OAM
DMA transfers, nine mapper writes, matching streaming/reference pictures, and
an unchanged source hash.

The full selected-source firmware build also passes its conformance and ELF
checks. MinimalBoot links with 23,296 bytes available for locals/stack. RAM2
contains 186,912 bytes of system variables and retains a 337,376-byte heap
reserve; none of the NESVM storage is placed there.

## Current artifacts

- Flash-ready test firmware: `nes/build/package/firmware/NESVM-TEST-MPE_Firmware-V1.0.17-a5840293.hex`
  - 6,549,905 bytes
  - SHA-256 `a58402931b7119ce45c3d9dc975222ae347f6ebcf7d34e368577c4b10f8144de`
- Generic terminal: `nes/sd-card/NESVM.CRT`
  - 24,688 bytes
  - SHA-256 `80b868fe0bde2559a0b59977812c39664ab6e673f4f390050472803393828396`
- Ready-to-copy SD tree: `nes/build/package/sd-card`
- Authorized Crossbow demo:
  - 98,320 bytes, Mapper 11
  - SHA-256 `93c1eff05b4d39992c0fd05dce9bb3d5b8349ca3a2416717d75ef4336fc715ea`

These are local test artifacts, not a published official release. The firmware
uses the existing `V1.0.17` version configuration, so its hash is authoritative;
the filename alone is not enough to identify this NESVM candidate.

## Reproduce the bounded checks

Run from the maintained repository root:

```powershell
node nes/tools/nes.mjs audit
node nes/tools/nes.mjs test
node nes/tools/nes.mjs layout
node nes/tools/nes.mjs run --rom nes/DEMO/Crossbow.nes --frames 300 --name firmware-ready
./nes/tools/build_nesvm.ps1
node nes/tests/nes_firmware_source_test.mjs nes/build/firmware/source
./scripts/build-firmware.ps1 -SourcePath nes/build/firmware/source -OutputRoot nes/build/firmware -CustomGuiAcmePath build/toolchain/acme-0.97-r20/acme0.97win/acme/acme.exe
```

`build_nesvm.ps1` creates the CRT and ignored distribution tree from an explicit
four-file list. It reads only `nes/DEMO/Crossbow.nes`, never the private
`nes/ROMS` directory.

## Remaining acceptance gates

1. Flash the candidate and verify that the former `WAITING FOR TEENSY` screen
   is replaced by the ROM list on a physical NTSC C64.
2. Launch Crossbow, confirm port-2 A, Space=B, Return=Start, Shift=Select, and
   Return+Shift back to the same list row.
3. Check both complete-frame presentation modes for legibility and tearing.
4. Listen for SID output and assess timing/quality. The basic register mapping
   is integrated, but NES envelopes, sweeps, triangle linear-counter behavior,
   exact noise, and DMC audio are not complete.
5. Repeat the launch/input/video/audio/exit checks on a C128 in C64 mode and run
   longer sessions to expose bus, SD, stack, or lifecycle failures.

No commit, sync, release, or firmware flash was performed for this candidate.
