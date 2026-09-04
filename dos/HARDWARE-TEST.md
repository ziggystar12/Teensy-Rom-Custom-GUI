# DOSVM hardware test

Use the committed files under **`dos/sd-card/`** with the current firmware in
**`firmware/`**. R15 uses the current V1.0.12 GUI and firmware and supports
the standard TeensyROM memory configuration without optional PSRAM.

## R15 direct-RAM change awaiting hardware acceptance

R15 maps guest `00000h-7FFFFh` directly onto all 512 KiB of RAM2. It removes
the SD page cache, `DOSVM.SWP`, and R14's unconditional two-millisecond yield.
The CPU keeps a 25,000-instruction ceiling and yields early for input, display
ACKs, and four-sector disk boundaries. It is compiled at `-O3`.

The post-link gate verifies 21,408 bytes of stack, live DOS/MPE/SD state in
RAM1, and 55 RAM2 symbols that are all inactive after takeover. The integrated
host test passed two reset-separated boots, `DIR`, keyboard editing, PCTONE,
Boulder rendering and movement, plus a cold Sierra launch. A comparable
nine-run host boot median improved from 424.691 ms for R14 to about 113 ms for
R15. These checks do not establish physical speed or stability.

Firmware V1.0.11 first replaced the duplicate 64 KiB native allocations with
one owned arena. V1.0.12 retains that layout, which increases the normal
pre-DOS RAM2 heap from 271,840 to 337,376
bytes. DOS claims and seals the arena before clearing RAM2, so the guest still
receives one contiguous 512 KiB and its 357,824-byte validated free block is
unchanged. This adds startup headroom and a checked ownership transition; it
does not increase DOS memory or measured VM speed.

Before ownership commits, R15 now drains and flushes every USB1 endpoint,
stops and resets the controller, verifies the endpoint state is idle, and then
disables USB polling. Every wait is bounded; a stuck controller rejects DOS
startup while RAM2 is still intact.

DOS is now a reset-only session. Leaving bank 58 or pressing the cartridge
button must reboot the Teensy and return to the GUI; it must not resume shared
firmware out of overwritten RAM2.

The R15 hardware-candidate files have these SHA-256 hashes:

- Firmware `MPE_Firmware-V1.0.12.hex`:
  `fd31dcc2d6dc84fddacaa6f18f2c12ef18a6113f58f672346c7d475e32ccf309`
- Cartridge `DOSVM.CRT`:
  `7438e8715f07c0dadf687f57989641cc98d23a96c29fb68579a07b95bacd10d1`
- Disk `DOSVM.IMG`:
  `9b92715061c496a05466ad29d9697a717287fb6b6eaec1c4b4a6f850426ce9d4`

`dos/SHA256SUMS.txt` verifies the matching firmware, cartridge, and disk. The
candidate is not a physical acceptance record until the steps below pass on
the cartridge.

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

## R13 scheduling comparison and failed hardware result

The user reported about ten seconds before the first DOS screen, letters
requiring two or three presses, and an estimated two to three times slower
than the earlier build. R13 failed physical responsiveness acceptance. The
following historical benchmark measured work per packet, not elapsed speed;
it missed input blind intervals and the time spent before each publication.

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
service interval. `dos/tools/test_mpe5_performance.ps1` compares R12 with the
current working tree after a firmware build, rather than recreating this
historical R13 run; it uses the existing `build/dos-work` files and writes
`build/dos-work/dos-performance-result.txt`. Do not run it concurrently with
a firmware build because it temporarily substitutes the staged scheduler.

## Repeat the hardware check

1. Flash `firmware/MPE_Firmware-V1.0.12.hex`.
2. Copy all contents of `dos/sd-card/` to the SD root. This includes
   `/DOSVM.CRT` and `/DOSVM/DOSVM.IMG`; R15 has no swap file.
3. Launch `DOSVM.CRT` from the GUI. The loader says **MHS DOSVM**, and its
   diagnostic title contains **R15**. Update both the firmware and CRT.
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
7. Leave DOS and confirm that the Teensy reboots to the launcher. Repeat the
   DOS launch, reboot out again, then check that a previously working Sierra
   game cold-launches with this exact firmware.

The virtual C: image remains read-only. The native core sends idle frame
packets while working.

If startup stops at a diagnostic, record the title, stage, error, packet
count and control bytes. Firmware `CTRL FB=04` means no safe reset-only RAM1
workspace could be acquired. `CTRL FB=05` indicates a BIOS/disk
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

For these runtime codes, F8/ F9/ FA give the failing guest address low/mid/high.
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

## R13 corrections and remaining regression

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
and emulated-terminal results. The subsequent hardware report above rejects
R13 responsiveness; these passes did not establish physical playability.

## R14 responsiveness correction

R13 stopped scanning the C64 keyboard while a previous input snapshot waited
for its acknowledgement. A test with three-raster-frame S/A/B taps and a
twelve-frame ACK delay reproduced the loss of A and B. R14 captures keys in
the raster IRQ and retains 31 queued states independently of the immutable
mailbox payload. The same test receives all six press/release states. Tests
also cover queue-full release recovery and preserved IRQ registers, flags,
stack and transfer pointers. Capture takes at most 315 instructions in the
tested matrix states.

The PC core formerly delivered one queued event per 20,000-instruction timer
poll. Native events now dispatch separately, retaining interrupt/prefix
guards. After each IRQ1 return the guest gets 512 instructions with interrupts
enabled before another transition. The regression delivers 24 queued events
in 16,000 instructions with application progress between them; the BIOS timer
continues advancing. The integrated FreeDOS command checks now use native
make/release mailbox messages, including `ECHO AABBCC` and Backspace.

R14 restores the 25,000-instruction ceiling and checks the Teensy DWT cycle
counter every 64 guest yield boundaries, targeting two milliseconds per
foreground call. Input and display acknowledgements cause an earlier yield.
The existing storage-operation limit remains; synchronous SD work can exceed
the target until the instruction and next clock checkpoint finish.

`test_mpe5_latency.ps1` compares R13's pinned scheduler with R14 using the
actual firmware publisher and identical current core/input. At an explicitly
modeled three microseconds per guest yield boundary, publishing a ready
1,000-cell screen took 7.9765 seconds under R13 versus 0.138436 seconds under
R14, including 0.5 milliseconds of modeled transfer/ACK time per packet.
An ACK or input arriving at 501 microseconds was serviced at 150,000 under
R13 and 501 under R14. One- and ten-microsecond costs and clock wrap also pass.
These figures reproduce the scheduling stall; they do not measure physical
DOS boot time, SD latency, or game speed. The report is packaged as
`DosTest/dos-latency-result.txt`. R14 hardware acceptance remains outstanding.

The final R14 build passed the full gate and verified all 16 packaged hashes
and 16 compiled native DOS source hashes. It retains the 16,384-byte stack
reserve and 263,744-byte RAM2 heap reserve. The integrated gate accepted 80
input events, returned DOS prompts, and passed Sierra cold/relaunch. Its
5,800 CGA frames include idle publications; their count is not a speed metric.

- Firmware `MPE_Firmware-V1.0.9.hex`:
  `78080080104fc04965ef9a2ce7fa1750e5c035563da51c96501089660a1abc7b`
- Cartridge `DOSVM.CRT`:
  `8260107c276a1673a0c00d68752826d5ecc4469c3059748555c337b3579777cb`
- Disk `DOSVM.IMG`:
  `9b92715061c496a05466ad29d9697a717287fb6b6eaec1c4b4a6f850426ce9d4`
