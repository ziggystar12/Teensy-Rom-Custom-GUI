# DOSVM hardware test

Use **`DosTest/` at the repository root**. R13 uses the released 1.0.9 GUI and
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

## R13 scheduling comparison

The host benchmark substitutes only R12's 25,000-instruction slice and three
scheduling functions from commit `129badcb4131192449b6605358e26d3e58d6855c`.
Both variants use the same corrected game-port and keyboard implementation,
start Boulder with Space then Shift, and retain three lives with neutral
input. R13 coalesces speaker updates and permits 50,000 instructions per
foreground slice, retaining the existing storage and ownership yields.

| Foreground polls per packet ACK | R12 instructions/packet | R13 instructions/packet | Ratio |
| --- | ---: | ---: | ---: |
| 1 | 37,575 | 95,965 | 2.55x |
| 3 | 59,986 | 195,325 | 3.26x |
| 9 | 245,223 | 495,975 | 2.02x |

At three polls, the measured game work was 25,014,568 instructions in 417
packets versus 25,196,940 in 129 packets. Waiting for the final SID can
overshoot the instruction target, so the ratios normalize by executed work.
R13 had zero stalled pending polls. Its visible bitmap matched 1,000/1,000
current cells at one/three polls and 998/1,000 at nine polls (two animation
cells). PCTONE produced an audible state, silence and a returned prompt in
all cases. The paging counts were unchanged at 153 reads/407 writes.

These are deterministic transport-work measurements, not hardware FPS or a
physical 2x speed claim. The longer slice increases the maximum foreground
service interval. Repeat with `dos/tools/test_mpe5_performance.ps1` after a
firmware build; it uses the existing `build/dos-work` files and writes
`build/dos-work/dos-performance-result.txt`. Do not run it concurrently with
a firmware build because it temporarily substitutes the staged scheduler.

## Repeat the hardware check

1. Flash `DosTest/firmware/MPE_Firmware-V1.0.9.hex`.
2. Copy all contents of `DosTest/sd-card/` to the SD root. This includes
   `/DOSVM.CRT`, `/DOSVM/DOSVM.IMG`, and `/DOSVM/DOSVM.SWP`.
3. Launch `DOSVM.CRT` from the GUI. The loader says **MHS DOSVM**, and its
   diagnostic title contains **R13**. Update both the firmware and CRT.
4. Look for the FreeDOS `C:\>` prompt. Type `DIR` and check for Boulder and
   README. Test Backspace, Return, and a second `DIR`.
5. Type `PCTONE`: expect a short SID tone followed by silence and the DOS prompt.
   Type `BOULDER`: expect coloured title graphics. Press Space to skip the
   intro, then hold Shift to start. Use cursor keys to move; Shift grabs and Space
   pauses during play. Check that the field stops restarting and the player
   moves repeatedly in each direction. C64 Shift+cursor selects Up/Left.
6. If available, try a joystick in port 2: directions act as cursor keys and
   fire acts as Shift. Check that releasing a direction stops that held key,
   and that pressing Shift alone works. Compare responsiveness with R12;
   there is no measured physical speedup yet.
7. Return to the launcher and repeat the DOS launch. Then check that a
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

## R12 evidence and hardware failure

The final R12 build passed the fresh integrated host and C64 replay gates.
Boulder reached a cave after Space. The replay verified 784
multicolour frames, including 645 distinct frames, and all 806 SID frames;
423 carried audible speaker output. These captures established CGA/SID
publication, but did not establish correct game startup or movement: the
disconnected PC game port incorrectly read as a pressed button.

On 2026-09-03 the user reported that R12 reached the title and cave on
hardware, ran extremely slowly, drew the field three or four times, and
then showed a player that could not be moved. That is a rendering pass and
a control/playability failure, not a gameplay acceptance.

- Firmware `MPE_Firmware-V1.0.8.hex`:
  `6bc7d0ceb026b36752940d797d0fb3d8dd29f7bdce22604b7114668deb35c0be`
- Cartridge `DOSVM.CRT`:
  `8051b7bb11427121e178f78b4c96e6fa12392d2e78e6171d5c232a562919da1a`
- Disk `DOSVM.IMG`:
  `58bfe9a5b569831ddb87de606c3701e5c6097e81754980fd22c822ba6e855c9e`

## R13 corrections awaiting hardware verification

The unimplemented PC game port `201h` returned `00h`, which Boulder treats
as an active-low abort/fire button. Returning `FFh` removes the phantom
button. The earlier R12 cave capture must not be used as movement proof.
The correct control sequence is Space to skip the intro, then hold Shift to
start. Cursor keys move, Shift grabs, and Space pauses during play.

The DOS terminal sends held PC scan-code snapshots with explicit releases
and independent Shift/Ctrl/Alt state. The native keyboard path generates
PC make/break events directly, avoiding ANSI sequences whose Escape byte
could be interpreted by a game. C64 Shift selects Up/Left and F2/F4/F6/F8
without adding an unwanted PC Shift modifier to those keys. Port 2 joystick
directions map to cursors; fire maps to Shift. This is keyboard translation,
not PC joystick emulation. F9 and higher are outside this milestone.

R13 also coalesces speaker changes at packet boundaries instead of stopping
the guest CPU at every audible edge. Pending wire packets stay unchanged
until ACK. This removes a source of transport-bound stalls; neither a
twofold physical speedup nor playable hardware performance has been measured.

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
The hardware checks are the prompt, PCTONE sound, visible CGA graphics,
stable game startup, sustained movement, and continued Sierra compatibility.
The earlier slowness/runtime-failure report
still needs a repeat with the detailed diagnostics if it recurs.

## Final R13 kit, 2026-09-03

The complete build passed and replaced `DosTest/` using the released 1.0.9
base. All 15 package checksums and 16 compiled native DOS source hashes match.
The firmware retains a 16,384-byte stack reserve and a 263,744-byte RAM2 heap
reserve; both SD File objects have ordinary startup initialization.

- Firmware `MPE_Firmware-V1.0.9.hex`:
  `49f1320c31c833df5bb33a755a69962dd531a9989c606f07c4c7e639c2dec199`
- Cartridge `DOSVM.CRT`:
  `0a5b84312eeb79f59889a63eef4a5a445070819bc173a3e92d6793c5fda28d24`
- Disk `DOSVM.IMG`:
  `9b92715061c496a05466ad29d9697a717287fb6b6eaec1c4b4a6f850426ce9d4`

The exact game passed 72.9 million instructions of startup, neutral input,
arrow movement, release, Shift grab and port-2 controls while retaining all
three lives. The integrated firmware gate passed 673 DOS packets, 94 Boulder
CGA frames, 30 audible SID frames and Sierra cold/relaunch. The C64 replay
verified 91 graphics frames, 107 exact SID register frames, and 56 keyboard
snapshots including delayed acknowledgements and releases. These are host
and emulated-terminal results; physical R13 playability remains unverified.
