# DOSVM

FreeDOS, CGA graphics, PC speaker output and Boulder gameplay work on physical
TeensyROM hardware. R16 played well before a transport error stopped the game
about ten seconds into play. R17 retains its speed improvements and addresses
that failure. **R17 hardware stability has not yet been confirmed.**

## Install R17 with firmware V1.0.13

Use the matching firmware, CRT and disk from the single `DosTest/` folder:

1. Flash `DosTest/firmware/MPE_Firmware-V1.0.13.hex`.
2. Copy `DosTest/sd-card/` contents to the SD root. The resulting files are
   `/DOSVM.CRT` and `/DOSVM/DOSVM.IMG`.
3. Launch `DOSVM.CRT` from the GUI. Its diagnostic title contains **DOSVM R17**.
4. At `C:\>`, try `DIR`, `VER`, `PCTONE`, then `BOULDER`.
5. In Boulder, press **Space to skip the intro, then Shift to start**. Cursor
   keys move, Shift grabs, and Space pauses. Port 2 joystick directions act
   as cursor keys; fire acts as Shift.

`DosTest/` contains only five files: its README, checksum manifest, firmware,
CRT and disk image. The matching published copies are `firmware/` and
`dos/sd-card/`; [SHA256SUMS.txt](SHA256SUMS.txt) records their hashes. The older
V1.0.12 release under `releases/native20/` remains unchanged. Follow
[HARDWARE-TEST.md](HARDWARE-TEST.md) for sustained play and update checks.

V1.0.13 also fixes the GUI firmware preflight path that reported “Firmware
selection changed” for an unchanged file. It reads and fingerprints the HEX
without issuing separate SD status commands during file streaming. File
identity, size, clean EOF, cancellation and CRC checks remain enforced. To
install this fix from an affected older firmware, use the working classic
**V** text updater; then test the corrected GUI updater.

## R17 transport recovery

The R16 photo shows stage 05, error `0C`, 1,071 accepted packets and
“UNSTABLE PACKET COMMIT.” The displayed fixed signature is XOR `08` away from
its expected bytes. This is evidence of corrupted reads; it does not establish
that the firmware changed a published packet.

After a failed read, the R17 terminal sends command `04` to request a quiet
retry. Firmware finishes its current VM slice, pauses further foreground VM
work, and publishes status `12` when the same packet can be read again. Only
the matching packet acknowledgement releases normal execution. The existing
packet CRC and bounded retries remain: persistent corruption still produces
a diagnostic instead of being accepted. Normal successful transfers retain
R16's scheduling and direct-memory fast paths.

## Memory, speed and controls

Guest addresses `00000h-7FFFFh` map directly onto all **512 KiB of RAM2**.
There is no page cache or `DOSVM.SWP`; only the read-only disk image is needed.
The BIOS reports 512 KiB, and the tested FreeDOS MCB chain has 357,824 bytes
free after repeated `DIR` commands without progressive loss.

DOS is a reset-only session. Live DOS, SD and MPE state stays in RAM1; USB DMA
is stopped before RAM2 is cleared. Leaving the cartridge bank or pressing the
cartridge button reboots the Teensy into the GUI. The link gate checks that
RAM2 contains no remaining live firmware state and that at least 16 KiB of
stack remains. Sharing the native-engine arena gives the normal pre-DOS heap
337,376 bytes; DOS still owns exactly 512 KiB after takeover.

R16 introduced direct opcode and operand access to RAM2/F000 and placed the
two small operand helpers in ITCM. R17 retains those changes. Interleaved host
A/B tests of identical guest work measured **1.86x faster boot and 1.96x faster
`DIR` than R15**. Those are host CPU measurements, not physical game frame
rates or a 286-equivalent rating. The complete interpreter remains in flash
to preserve RAM1 space. The CPU uses `-O3`, a 25,000-instruction ceiling,
immediate input/ACK yields and four-sector disk boundaries.

Shift and cursor-key releases wait until their press has been visible for at
least 550,000 guest instructions, allowing Boulder's sparse input polling to
see quick taps. Printable key pairs retain their short 512-instruction
cadence. The C64 scans input in raster IRQs into a bounded queue, independently
of packet acknowledgement waits. Both Shift keys, Ctrl, Commodore/Alt and
F1-F8 are covered; C64 Shift+cursor selects Up/Left. F9 and higher are outside
this milestone. Port 2 is translated into keyboard state, not a PC joystick.

## Display, sound and disk

DOS text is **320x200 hires**, white on black, using 8x8 glyphs and 40 visible
columns. The BIOS retains an 80-column console, so its right half is clipped.
A software 80-column renderer would require narrower glyphs and is not
implemented. ASCII includes lowercase and punctuation; extended CP437 is not
implemented.

CGA modes 4/5 reduce the PC's 320x200 image to C64 160x200 logical multicolour
pixels. Mode 6 reduces 640x200 monochrome to 320x200 hires. Display start,
blanking, palette and intensity are reflected in the nearest C64 colours.
Rendering uses a private VRAM mirror without extra SD reads or reduced DOS
memory. Mode changes replace the complete picture before displaying it.

`PCTONE` tests PC speaker pitch/gate output through SID voice 1. Rapid changes
can be coalesced at display-packet boundaries; this is not sampled audio.
The kit uses NTSC SID pitch tuning; PAL pitch is slightly lower.
[Tandy modes 08h/09h](TANDY-VIDEO-PLAN.md), EGA and VGA are not implemented.

`/DOSVM/DOSVM.IMG` is a read-only virtual C: drive: a 1.44 MiB FAT12 FreeDOS
volume inside a 1,516,032-byte MBR image. Sectors are read from SD rather than
loading the entire disk into RAM. It includes FreeCOM, startup configuration,
`CGA40.COM`, `PCTONE.COM`, `README.TXT` and `BOULDER.EXE`. The CRT contains the
C64 terminal and BIOS. Writable disks and Might and Magic remain later work.

## Build the latest test

From the repository root:

```powershell
.\dos\tools\build_dos_test.ps1
```

The script reuses `build/dos-work/`, reads `firmware-version.json`, builds the
firmware, CRT and disk, and replaces `DosTest/` only after its gates pass. It
does not create numbered test folders or publish a release by itself.

The default read-only inputs are:

- FreeDOS: `E:\MHS-Repository\HamsterOS\build\freedos\FDT2607-FloppyEdition.zip`
- Boulder: `E:\MHS-Repository\HamsterOS\dos\Boulder.exe`

Use `-FreeDosZip` and `-Boulder` to override them. Python, Node.js, a Windows
C++ compiler, the firmware toolchain and the sibling `AGI-64` checkout are
required. `-Compiler` and `-ToolchainRoot` override tool locations. FreeCOM is
cached once under `build/dos-work/freecom/` and its pinned hashes are checked.

Tests cover the real firmware sequencer, direct memory, reset ownership,
repeated boots/commands, input editing, CGA, speaker output and Sierra
cold/relaunch. C64 CPU replay executes the packaged terminal against firmware
packets and injected read failures. Host and emulator checks do not establish
physical expansion-bus timing or sustained hardware playability.

To repeat the native VM check against the current disk:

```powershell
.\dos\tools\test_mpe5_vm.ps1 -Image .\DosTest\sd-card\DOSVM\DOSVM.IMG
```

## Brief hardware history

- R10 reached the FreeDOS prompt and accepted commands; Boulder lacked graphics.
- R12 reached CGA graphics but was slow and had control problems. R13's longer
  CPU slices worsened responsiveness and lost short key presses.
- R14 corrected input capture and scheduling while still using SD-paged RAM.
- R15 replaced paging with 512 KiB direct RAM2. The user confirmed Boulder
  starts after Space followed by Shift, but performance remained slow.
- R16 retained working gameplay and improved perceived speed; the user then
  reported the transport error after about ten seconds. R17 addresses that
  failure and requires a new sustained hardware test.
