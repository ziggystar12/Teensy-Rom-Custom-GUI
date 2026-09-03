# DOSVM

The goal is a FreeDOS `C:\>` prompt launched from the TeensyROM+ GUI, with
`DIR` entered on the C64 keyboard. Boulder Dash is the next graphics test;
Might and Magic follows once the prompt and CGA graphics work on hardware.

## Install the current R15 build

R15 requires the matching [MPE Firmware V1.0.11](../firmware/MPE_Firmware-V1.0.11.hex?raw=true),
[DOSVM.CRT](sd-card/DOSVM.CRT?raw=true), and read-only
[DOSVM.IMG](sd-card/DOSVM/DOSVM.IMG?raw=true). Do not mix the R15 cartridge
with an older firmware build.

1. Flash `firmware/MPE_Firmware-V1.0.11.hex`.
2. Copy the contents of [`dos/sd-card/`](sd-card/) to the TeensyROM SD root.
   The resulting paths must be `/DOSVM.CRT` and `/DOSVM/DOSVM.IMG`.
3. Start `DOSVM.CRT` from the GUI. Its diagnostic title contains **DOSVM R15**.
4. At the FreeDOS `C:\>` prompt, type `DIR`, `VER`, or `PCTONE`. Type
   `BOULDER` for the included CGA test.

The exact matching hashes are in [SHA256SUMS.txt](SHA256SUMS.txt). Follow
[HARDWARE-TEST.md](HARDWARE-TEST.md) for the complete cartridge checks.

## R15 direct-RAM build

R15 gives FreeDOS 512 KiB of conventional memory by mapping guest addresses
`00000h-7FFFFh` directly onto the Teensy 4.1's entire RAM2 range. It has no
page cache and no `DOSVM.SWP`; only the read-only `DOSVM.IMG` disk is needed.
The BIOS reports 512 KiB through INT 12. The validated DOS MCB chain has
357,824 bytes free after repeated `DIR` commands, with no progressive loss.

This is a reset-only session. RAM2 formerly held the heap and inactive shared
engines, so leaving the DOS cartridge bank or pressing the cartridge button
reboots the Teensy and returns to the GUI. The linked image keeps live DOS,
SD and MPE transport state in RAM1, stops USB DMA before clearing RAM2, and
the current 1.0.11-linked build retains 21,408 bytes for the MinimalBoot stack.
Its shared 64 KiB native arena removes a duplicate RAM2 allocation, increasing
the normal pre-DOS heap from 271,840 to 337,376 bytes. Once DOS starts, that
arena becomes guest memory along with the rest of RAM2, so conventional memory
remains exactly 512 KiB.

The CPU uses `-O3`, a 25,000-instruction ceiling, immediate input/ACK yields,
and four-sector disk boundaries. The R14 two-millisecond deadline is gone.
In the comparable host boot harness, a nine-run R15 median is about 113 ms;
the earlier R14 median was 424.691 ms. This 3.75x host result measures the
software path and does not establish a physical 8086/286-equivalent speed.

CGA, PC speaker, held keyboard input and port-2-to-cursor translation remain.
[Tandy modes 08h/09h](TANDY-VIDEO-PLAN.md) are the next graphics tier;
they are not present in R15. A stock VIC-II cannot show arbitrary 16 colours
per cell, so that work will use deterministic per-cell palette reduction.

## Historical R10-R14 notes

**R10's DOS prompt and command entry were confirmed on physical hardware
on 2026-09-02.** The exact firmware/cartridge/disk hashes are recorded in
[HARDWARE-TEST.md](HARDWARE-TEST.md). R10 lacked CGA output; R12 adds the
renderer and PC-speaker-to-SID output. The R12 hardware test reached its title
and cave, but was very slow, repeatedly drew the field, and did not allow
movement. R13 regressed on hardware: about ten seconds to the first DOS
screen, two or three presses to register letters, and an estimated two to
three times slower. Its instructions-per-packet benchmark did not predict
elapsed speed or input latency. R14 addresses the input blind interval and
long foreground CPU slices; physical acceptance remains outstanding.

**R14 targets the standard cart without optional PSRAM**, using the released
1.0.9 GUI and firmware base. The earlier memory error `04` came from requiring
an optional expansion that the standard TeensyROM configuration does not
promise. Its 512 KiB REU feature uses internal RAM; it is not evidence of
PSRAM. The current build replaces the oversized flat allocation with SD paging.

R9 failed on hardware after its first transport packet. We reproduced that
failure before any DOS disk read by compiling the host VM with the Teensy
compiler's unsigned plain-char default. The BIOS's `JL -39` instruction at
`F000:02EF` was incorrectly treated as `JL +217`. R10 makes signed x86 byte
operations explicit. The VM regression now runs with both char defaults;
the integrated firmware and C64 replay use the unsigned-char build. The
shared AGI terminal's waiting and packet retry behavior is unchanged.

The native core still presents 640 KiB conventional RAM. A 148 KiB page
cache, 5,810 bytes of metadata, a permanent 64 KiB F000 segment, and 6,000
bytes of console buffers fit in **228,912 resident bytes**, including alignment.
Firmware borrows only the validated unused tail of `RAM_Image`, after the
DOS CRT's three 8 KiB pages. It preserves the loaded cartridge, existing
stack reserve and RAM2 heap. Guest RAM, CGA memory and I/O latches use
address-based access; no CPU operand holds an evictable cache pointer.

`/DOSVM/DOSVM.SWP` is a separate 1,185,792-byte scratch backing file. The kit
preallocates it to avoid a large startup write. Copy it with the SD files and
keep the SD card writable. Every launch invalidates the in-memory page map,
so stale scratch data cannot become new guest RAM. Paging trades speed for
compatibility with the standard hardware; game performance is unmeasured.

R11 also runs bounded CPU slices while the C64 displays an already-published
packet, preserving its bytes until ACK. A larger cache reduced scratch-page
transfers by 31.6% during boot and 54.1% for the first `DIR` in an isolated
R10 comparison. These transfer counts are not a measured hardware speedup.
Scratch-page I/O gets one full-page retry before a typed runtime failure.
The later R10 hardware failure after `VER`/`SETUP` has not been reproduced
on the host; new diagnostics distinguish stopped CPU, guest-memory bounds,
and individual swap operations. See [HARDWARE-TEST.md](HARDWARE-TEST.md).
Twelve repeated host `DIR` commands keep 488,896 guest bytes free after the
first listing, with no progressive loss; this does not establish the cause
of the reported physical failure.

DOS text is 320x200 hires, white on black, with an 8x8 font and 40 visible
columns. The BIOS console shadow retains 80 columns, so long lines are
currently clipped on the right. A software 80-column renderer would need
4-pixel-wide glyphs packed in pairs; it is not yet implemented.

CGA modes 4/5 publish a four-colour C64 multicolour bitmap: the guest's
320x200 image is reduced to 160x200 logical colour pixels. Mode 6 reduces
640x200 monochrome to 320x200 hires. Colour selection, intensity, display
start and blanking are mirrored; the C64 uses its nearest available colours.
The 26,509-byte video workspace reuses the BIOS staging arena after the BIOS
copy. Guest VRAM writes update a private mirror, so rendering adds no SD reads
and does not shrink DOS RAM or the page cache. Mode changes send a complete
replacement before showing the new picture.

`PCTONE` tests PC speaker tones through SID voice 1. R13 coalesces PIT changes
into speaker snapshots at display-packet boundaries, allowing the CPU to
continue while the C64 receives a packet. R12 paused the CPU at each audible
change, tying guest progress to packet delivery. Very short intervening tones
can be coalesced; this is PC-speaker pitch/gate output rather than sampled
audio. EGA is not implemented. The test kit uses NTSC SID pitch tuning
(PAL will play slightly lower). R13 was slower in the user's hardware test.

R14 restores the 25,000-instruction ceiling and adds a two-millisecond
foreground budget measured by the Teensy's cycle counter. It yields when
input or a pending display acknowledgement arrives. Synchronous SD work
can exceed that target until the current instruction completes. The old
50,000-instruction slices ran before each cell packet, delaying publication
even when its pixels were already available. R13's packet-count comparison
is historical evidence, not a speed acceptance gate.

The repeated Boulder restarts had a separate cause: the VM returned zero for
the unimplemented PC game-port address `201h`, making its active-low button
look permanently pressed. R13 returns `FFh` for that disconnected port.
R12's title/cave captures proved drawing and transport, but not correct
control or movement. Its older frozen BIOS-clock bug is already fixed:
the Teensy supplies elapsed milliseconds so guest countdowns advance.

DOS input now carries held PC scan codes and modifier state, including key
releases. Cursor input no longer goes through ANSI Escape sequences. The
C64's Shift+cursor combinations select Up and Left; Shift by itself remains
available to games. Both Shift keys, Control, Commodore/Alt, and F1-F8 are
covered by the input tests. Printable keys and Backspace retain repeat.
Port 2 joystick directions act as cursor keys, and fire acts as Shift.
This translates joystick input to keyboard state; it does not emulate a
PC joystick. F9 and higher are outside this milestone.

R14 captures keyboard state on each raster interrupt into a bounded queue,
independently of foreground packet and input-ACK waits. R13 stopped scanning
while an input acknowledgement was pending, so brief taps could disappear.
Native PC key events are serviced separately from the slower BIOS timer
poll. The integrated DOS command test now uses the same make/release mailbox
messages as the physical terminal, including consecutive repeated letters.

For Boulder, press **Space to skip the intro**, then **hold Shift to start**.
Use cursor keys to move; Shift is the grab action, and Space pauses during
play. The port 2 joystick provides movement and Shift through its fire
button. Physical movement and playability still need verification.

The CRT header now says `MHS DOSVM`. Both native firmware loaders accept
that exact name and the original Sierra name; update firmware and CRT together.
The DOS terminal adds a background-colour byte to its SID frame packet using
a checked build overlay. It leaves the shared AGI terminal source unchanged.

The build gates exercise this same paged path without PSRAM, repeated boot,
`DIR`, Backspace, a returned prompt, scratch-file failures and Sierra
relaunch. Host execution and C64 replay do not establish physical cart timing.

## Build and replace the latest test

From the repository root:

```powershell
.\dos\tools\build_dos_test.ps1
```

This single entry point reuses `build/dos-work/` for the firmware checkout,
cached FreeCOM files, and generated media. It reads `firmware-version.json`
through the existing version helper, builds that firmware and the CRT,
checks the manifests, and runs publication regressions and host acceptance
on the staged SD image. The integrated test uses the staged firmware source,
packaged CRT, and packaged image together. A headless VICE audit also runs
the CRT's C64 boot code through terminal startup. After the integrated test
returns a complete prompt, its wire trace is replayed through the actual
C64 terminal to verify display and keyboard behavior. A second replay checks
Boulder's CGA cells and the PCTONE SID updates, including hidden mode changes.
Keyboard gates exercise held keys, releases, modifier transitions, typematic
repeat, and the actual C64 matrix/port 2 scanner, including delayed ACKs.
`host-screen.png` and `boulder-screen.png` come from those executed C64 planes.
Only after all gates
pass does it replace `DosTest/`. It does not create numbered test folders or
update the production firmware and releases.

The default read-only inputs are:

- FreeDOS ZIP: `E:\MHS-Repository\HamsterOS\build\freedos\FDT2607-FloppyEdition.zip`
- Boulder: `E:\MHS-Repository\HamsterOS\dos\Boulder.exe`

Use `-FreeDosZip` and `-Boulder` to supply different locations. The script
requires Python, Node.js, the firmware toolchain, a Windows `g++` compiler,
and the sibling `AGI-64` checkout for the terminal generator and bundled VICE.
`-Compiler` and `-ToolchainRoot` override the compiler and firmware-toolchain
locations.
FreeCOM is downloaded once into `build/dos-work/freecom/`; its pinned hashes
are verified whenever the image is built.

To repeat only the host check against the current kit:

```powershell
.\dos\tools\test_mpe5_vm.ps1 -Image .\DosTest\sd-card\DOSVM\DOSVM.IMG
```

The replacement uses inline SdFat `FsFile` state in RAM1 and maps all 512 KiB
of RAM2 directly to conventional guest memory. Regression coverage includes
the reset-only handoff, USB shutdown, stale RAM2 clearing, all 1,000 unique
base cells in bounded packets, hires completion, idle packets, and actual C64
keyboard-matrix `DIR` and Return messages. The linked ELF must retain at least
16 KiB of stack; the 1.0.11 R15 build retains 21,408 bytes.
Host execution and VICE cannot establish physical TeensyROM+/C64 bus timing.

The native console buffers live outside the guest address map. The BIOS
already owns its B800/C000/C800 video pages; using those pages as host
display scratch caused repeated lines and cursor corruption. The regression
now checks an intact guest display, a clean directory listing, Backspace,
and a returned prompt, then replays the actual packets through the C64 CPU.

## What this milestone includes

The native 8086 adapter runs on Teensy, with one engine active at a time.
The guest has 512 KiB of direct conventional memory plus its BIOS and CGA
address regions. The C64 receives text cells and sends keyboard input through
the MPE transport. Returning to the GUI performs an MCU reset.

The C64 text display is a 320x200 hires bitmap with 40x25 cells, showing the left
40 columns of the BIOS's 80-column text console. The preview is enlarged 2x
without smoothing. R8 uses a full 8x8 ASCII font with lowercase and
punctuation; R7's small uppercase font incorrectly replaced commas with `?`.
Extended CP437 characters are not yet supported.

`/DOSVM/DOSVM.IMG` is a read-only virtual C: drive. It contains a 1.44 MiB
FAT12 FreeDOS volume inside a 1,516,032-byte MBR disk image. Sector reads come
from SD; the whole image is not loaded into guest RAM. The builder verifies
the source archive, inserts FreeCOM, startup configuration, `CGA40.COM`,
`PCTONE.COM`, `README.TXT`, and `BOULDER.EXE`, then validates their FAT chains and hashes.
The CRT carries the C64 terminal and pinned BIOS, while FreeDOS stays on SD.

The hardware gate is launch, prompt, `DIR`, Return, Backspace, CGA output,
PC-speaker sound, and sustained movement using the keyboard or port 2.
Writable storage, PC joystick emulation, Tandy video, EGA,
and the supplied Might and Magic files remain later work. Host success
establishes the VM-to-buffer path; it does not establish physical C64 bus
timing or playable games.
