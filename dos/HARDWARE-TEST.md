# DOSVM hardware test

Use **`DosTest/` at the repository root**. R12 uses the released 1.0.8 GUI and
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
3. Launch `DOSVM.CRT` from the GUI. The loader says **MHS DOSVM**, and its
   diagnostic title contains **R12**. Update both the firmware and CRT.
4. Look for the FreeDOS `C:\>` prompt. Type `DIR` and check for Boulder and
   README. Test Backspace, Return, and a second `DIR`.
5. Type `PCTONE`: expect a short SID tone followed by silence and the DOS prompt.
   Type `BOULDER`: expect coloured title graphics. Try Space to advance to
   the cave, as in the host test. Full keyboard gameplay is not a passed milestone.
6. Return to the launcher and repeat the DOS launch. Then check that a
   previously working Sierra game still launches with this exact firmware.

The SD card must be writable for the scratch file. The virtual C: image
remains read-only. Old scratch contents are ignored on every launch; it is
not a saved VM state. Paging can make boot slower than the earlier flat
host simulation. The native core sends idle frame packets while working.

If startup stops at a diagnostic, record the title, stage, error, packet
count and control bytes. Firmware `CTRL FB=04` now means no safe cartridge
RAM tail could be acquired. `CTRL FB=05` indicates a BIOS/disk/scratch-file
startup error in the current build. R10 also used it for runtime failures;
the current runtime codes are listed below.
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

## Current R12 host evidence

The final R12 build passed the fresh integrated host and C64 replay gates.
Boulder reached its first cave after Space. The replay verified 784
multicolour frames, including 645 distinct frames, and all 806 SID frames;
423 carried audible speaker output. This is host evidence; R12 hardware
acceptance is still pending.

- Firmware `MPE_Firmware-V1.0.8.hex`:
  `6bc7d0ceb026b36752940d797d0fb3d8dd29f7bdce22604b7114668deb35c0be`
- Cartridge `DOSVM.CRT`:
  `8051b7bb11427121e178f78b4c96e6fa12392d2e78e6171d5c232a562919da1a`
- Disk `DOSVM.IMG`:
  `58bfe9a5b569831ddb87de606c3701e5c6097e81754980fd22c822ba6e855c9e`

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

VICE checks the actual CRT boot code through terminal startup. A separate
deterministic 6510/CIA replay executes the actual terminal against captured
firmware packets to check complete 1,000-cell publication, 320x200 hires
rendering and keyboard messages. These host/emulator gates do not establish
physical TeensyROM/C64 timing.

R12 adds CGA graphics and PC speaker output. Four-colour graphics use the
C64's 160x200 multicolour mode; DOS text remains 320x200 hires. Mode 6 uses
320x200 hires after reducing the PC's 640 horizontal pixels. The kit's
`boulder-screen.png` is a host replay, not a photograph of physical hardware.
The hardware checks are the prompt, PCTONE sound, visible CGA graphics, and
continued Sierra compatibility. The earlier slowness/runtime-failure report
still needs a repeat with the detailed diagnostics if it recurs.
