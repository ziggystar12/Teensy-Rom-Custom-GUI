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
      DOSVM.JSON
      DOSVM.CRT.JSON
```

The package README identifies the firmware to flash and diagnostic title to
expect. Copy the contents of `DosTest/sd-card/` to the SD root, then select
`DOSVM.CRT`. Follow [HARDWARE-TEST.md](HARDWARE-TEST.md) for acceptance.

**Current evidence:** the released 1.0.5 firmware is confirmed to run Sierra
games on the user's hardware. The R6 DOS test rebooted to the GUI and failed
its physical launch test. The Sierra confirmation applies to released 1.0.5,
not R6. R6's image passed the native host test: FreeDOS reaches `C:\>`, accepts
queued `DIR`, finds the Boulder entry, and produces 1,000 CGA text cells.
The R6 CRT contained bank-58 ROM records that the native firmware parser
explicitly rejects. This is a reproduced cartridge-format error before DOS
startup. R7 passed the returned-prompt and C64 wire-replay host gates, but
the user's R7 hardware photo reports firmware memory error `04`: sufficient
PSRAM was not detected. The installed expansion hardware is unconfirmed.
R8 corrects the font and checks every visible character, including commas;
the PSRAM requirement is unchanged.

**Hardware requirement:** this DOS implementation requires the optional
Teensy PSRAM expansion. Its 640 KiB guest RAM uses a flat PC address map plus
I/O and private console storage totaling 1,185,632 bytes (about 1.13 MiB).
Flash capacity and SD disk capacity do not supply that working RAM. Native Sierra can run without
PSRAM, so Sierra working does not establish that the DOS memory is available.
The replacement rejects missing PSRAM before touching the arena and reports
firmware memory error `04`.

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
firmware's 16 KiB stack reserve. Regression coverage also includes missing
PSRAM rejection, all 1,000 unique base cells in bounded packets, hires frame
completion, idle heartbeats, and actual C64 keyboard-matrix `DIR` and Return
messages. These integration checks extend the earlier VM-to-buffer test.
Host execution and VICE cannot establish physical TeensyROM+/C64 bus timing.

The native console buffers live outside the guest address map. The BIOS
already owns its B800/C000/C800 video pages; using those pages as host
display scratch caused repeated lines and cursor corruption. The regression
now checks an intact guest display, a clean directory listing, Backspace,
and a returned prompt, then replays the actual packets through the C64 CPU.

## What this milestone includes

The native 8086 adapter runs on Teensy and shares the PSRAM arena used by
native AGI, with one engine active at a time. The guest has 640 KiB of
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
