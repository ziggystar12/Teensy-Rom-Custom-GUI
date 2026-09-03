# DOSVM hardware test

Use **`DosTest/` at the repository root**. R11 uses the released 1.0.8 GUI and
firmware base and supports the standard TeensyROM memory configuration
without optional PSRAM. Its README and `SHA256SUMS.txt` identify the exact kit.

## Confirmed R10 milestone

On 2026-09-02 the user confirmed that R10 reaches a working DOS prompt on
the physical TeensyROM/C64 and accepts the `BOULDER` command. Boulder then
clears the display to black; game graphics and gameplay are not yet accepted.
Sierra cold/relaunch passes the integrated host test; this report does not
claim a new physical Sierra regression check.

A later report in the same test session found severe slowness and a firmware
runtime failure after `VER` and `SETUP`: stage05, terminal error03, 0DD2
accepted packets, firmware error05, ACK DF and next commit E0. Boulder was
not run in that failing session; its separate black screen required reboot.
R10 is a confirmed prompt milestone, not a stability or gameplay pass.

The confirmed kit uses these SHA-256 hashes:

- Firmware `MPE_Firmware-V1.0.7.hex`:
  `b7a0e6676993d9e95f2814b80eb98b674b4d86d4b181fd83073efec01d9c0699`
- Cartridge `DOSVM.CRT`:
  `d93bb9969c005f2e6088cab7e39ab5166f9de84cbea78b1e17bc5a2be7b879cc`
- Disk `DOSVM.IMG`:
  `bbb3abec6e4e59ba46dd5502753a71670735f92b7343bbc5b0d831a80fff8695`

`DosTest/` remains the single local test kit. This confirmation applies to
the exact files above; rebuilding does not automatically confirm a new binary.

## Repeat the hardware check

1. Flash `DosTest/firmware/MPE_Firmware-V1.0.8.hex`.
2. Copy all contents of `DosTest/sd-card/` to the SD root. This includes
   `/DOSVM.CRT`, `/DOSVM/DOSVM.IMG`, and `/DOSVM/DOSVM.SWP`.
3. Launch `DOSVM.CRT` from the GUI. Its diagnostic title contains **R11**.
4. Look for the FreeDOS `C:\>` prompt. Type `DIR` and check for Boulder and
   README. Test Backspace, Return, and a second `DIR`.
5. Return to the launcher and repeat the DOS launch. Then check that a
   previously working Sierra game still launches with this exact firmware.

The SD card must be writable for the scratch file. The virtual C: image
remains read-only. Old scratch contents are ignored on every launch; it is
not a saved VM state. Paging can make boot slower than the earlier flat
host simulation. The native core sends idle frame packets while working.

If startup stops at a diagnostic, record the title, stage, error, packet
count and control bytes. Firmware `CTRL FB=04` now means no safe cartridge
RAM tail could be acquired. `CTRL FB=05` can indicate a BIOS/disk/scratch-file
startup error, a stopped guest CPU, or a guest-memory/page-store failure.
Include both the top-level terminal error and `CTRL FB`.
`PACKETS 0000` means no first packet was accepted; a higher count shows some
transport progress but does not identify the cause of a later failure.

R11 keeps startup codes02/04/05, but replaces generic runtime05 with:

| CTRL FB | Runtime failure |
| --- | --- |
| 40 | CPU was no longer ready without a captured reason |
| 41 | Guest reached CS:IP 0000:0000 |
| 42 / 43 | Invalid guest read / write span |
| 44 / 45 | Memory read / write callback failed |
| 46 | Invalid scratch page or unavailable scratch file |
| 47 / 48 | Scratch read seek / complete read failed after retry |
| 49 / 4A | Scratch write seek / complete write failed after retry |

For these runtime codes, F8/ F9/ FA give the failing address low/mid/high;
it is a scratch-file offset for swap failures and a guest address otherwise.
FC/FD give guest CS low/high; FE/FF give IP low/high. These replace the input
fields only once execution has failed. The error is deferred until the
current packet is ACKed; the firmware does not overwrite a pending packet.

`VER` followed by `SETUP` returns to the prompt in the host test, and `VER`
still works afterwards. The installer itself reports environment errors and
aborts. This has not reproduced the physical runtime error. R11 has larger
cache, CPU progress during pending display packets, one retry for a failed
swap transfer, and the detailed diagnostics above; hardware confirmation
of its speed and stability remains outstanding.

The R11 test firmware SHA-256 is
`938d97cc5d2b9c6a0d942975ff353443c691feb3caf975a247f13c6d6556bb99`;
its CRT is `0ae286a86ea357bcf47b8b811283f088ddceec29243ebf446ee9383f7400b5be`.
The kit's manifest records the full 1.0.8 inputs and checksums. Production
firmware and release snapshots are separate from this DOS test build.

The host regression also runs twelve `DIR` commands after a warm first
listing and validates the DOS memory-control-block chain at every prompt.
All twelve keep 488,896 bytes free and a 488,752-byte largest block. The first
listing changes allocation once compared with the initial prompt; repeated
listings show no progressive memory loss in this measured sequence.

R9 reached one accepted packet on hardware, then stopped with firmware error
`05`. The same first-slice failure was reproduced on the host by using the
Teensy compiler's unsigned plain-char default: BIOS instruction `7C D9` at
`F000:02EF` jumped forward instead of backward. R10 uses explicit signed
byte types for x86 displacements and arithmetic. Both signed- and unsigned-char
VM builds must now pass; the integrated firmware replay uses unsigned char.

The terminal already uses the shared AGI loader's bounded waits and packet
rereads. Its error `03` reports an explicit firmware failure; a packet wait
timeout is error `02`. VM instruction/I/O yields keep the VM alive and allow
later polling, so a slow boot does not itself produce firmware error `05`.

The build tests the actual firmware sequencer with simulated SD and bus pins:
no-PSRAM boots, poisoned old RAM/scratch contents, `DIR`, keyboard editing,
returned prompt, unchanged cartridge prefix and disk image, SD failure,
and Sierra cold/relaunch. Paging tests exercise dirty eviction, crossing page
boundaries, reset and I/O failure. The linked firmware must retain its
16 KiB stack and 256 KiB RAM2 heap reserves. Both SD `File` objects must have
ordinary startup initialization rather than reside in NOLOAD memory.

The actual C64 terminal is also checked in VICE for CRT startup, complete
1,000-cell publication, 320x200 hires rendering and keyboard messages.
These host/emulator gates do not establish physical TeensyROM/C64 timing.

CGA game graphics and PC-speaker output remain later milestones. The current
hardware pass condition is a usable DOS prompt with Sierra still working.
