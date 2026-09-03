# DOSVM

The goal is a FreeDOS `C:\>` prompt launched from the TeensyROM+ GUI, with
`DIR` entered on the C64 keyboard. Boulder Dash is the next graphics test;
Might and Magic follows once the prompt and CGA graphics work on hardware.

## Latest test build

Always use the repository's **[DosTest](../DosTest/README.md)** folder:

```text
DosTest/
  README.md
  SHA256SUMS.txt
  firmware/
  sd-card/
    DOSVM.CRT
    DOSVM/
      DOSVM.IMG
      DOSVM.SWP
      DOSVM.JSON
      DOSVM.CRT.JSON
```

The package README identifies the firmware to flash and diagnostic title to
expect. Copy the contents of `DosTest/sd-card/` to the SD root, then select
`DOSVM.CRT`. Follow [HARDWARE-TEST.md](HARDWARE-TEST.md) for acceptance.

**R10's DOS prompt and command entry were confirmed on physical hardware
on 2026-09-02.** The exact firmware/cartridge/disk hashes are recorded in
[HARDWARE-TEST.md](HARDWARE-TEST.md). Boulder currently clears the display to
black; its graphics and gameplay remain unfinished.

**R11 targets the standard cart without optional PSRAM**, using the released
1.0.8 GUI and firmware base. The earlier memory error `04` came from requiring
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

The display is 320x200 hires, white on black, with an 8x8 font and 40 visible
columns. The BIOS console shadow retains 80 columns, so long lines are
currently clipped on the right. A software 80-column renderer would need
4-pixel-wide glyphs packed in pairs; it is not yet implemented.

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
C64 terminal to verify display and keyboard behavior. Only after all gates
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

The replacement initializes small MPE5 controls and the SD `File` object in
ordinary RAM; `File` resides in `.data`. The bulk text, keyboard, and speaker
buffers stay in `NOLOAD` DMAMEM and are explicitly reset, preserving the
firmware's 16 KiB stack reserve. Regression coverage also includes no-PSRAM
boot, scratch isolation, all 1,000 unique base cells in bounded packets, hires frame
completion, idle heartbeats, and actual C64 keyboard-matrix `DIR` and Return
messages. These integration checks extend the earlier VM-to-buffer test.
Host execution and VICE cannot establish physical TeensyROM+/C64 bus timing.

The native console buffers live outside the guest address map. The BIOS
already owns its B800/C000/C800 video pages; using those pages as host
display scratch caused repeated lines and cursor corruption. The regression
now checks an intact guest display, a clean directory listing, Backspace,
and a returned prompt, then replays the actual packets through the C64 CPU.

## What this milestone includes

The native 8086 adapter runs on Teensy, with one engine active at a time.
Its page cache borrows unused cartridge RAM. The guest has 640 KiB of
conventional memory plus its BIOS and CGA address regions. The C64 receives
text cells and sends keyboard input through the MPE transport.

The C64 display is a 320x200 hires bitmap with 40x25 cells, showing the left
40 columns of the BIOS's 80-column text console. The preview is enlarged 2x
without smoothing. R8 uses a full 8x8 ASCII font with lowercase and
punctuation; R7's small uppercase font incorrectly replaced commas with `?`.
Extended CP437 characters are not yet supported.

`/DOSVM/DOSVM.IMG` is a read-only virtual C: drive. It contains a 1.44 MiB
FAT12 FreeDOS volume inside a 1,516,032-byte MBR disk image. Sector reads come
from SD; the whole image is not loaded into guest RAM. The builder verifies
the source archive, inserts FreeCOM, startup configuration, `CGA40.COM`,
`README.TXT`, and `BOULDER.EXE`, then validates their FAT chains and hashes.
The CRT carries the C64 terminal and pinned BIOS, while FreeDOS stays on SD.

The immediate hardware gate is launch, prompt, `DIR`, Return, and Backspace.
CGA game graphics, PC-speaker output, writable storage, joystick input, EGA,
and the supplied Might and Magic files remain later work. Host success
establishes the VM-to-buffer path; it does not establish physical C64 bus
timing or playable games.
