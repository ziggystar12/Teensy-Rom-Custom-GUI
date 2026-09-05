# Independent NES/DOS test — V1.1.1 / ABI 2

Date: September 4, 2026. This is a hardware-test candidate, not physical
performance acceptance. No hardware was flashed by the build or verification.

The NES-only V1.1.0 baseline was committed and synchronized on `main` as
`2baab38d7772d9e4748d8050d6a7e85597305be9`. The user reported SMB launching,
but severe slowdown and visible scanline-block drawing. That feedback remains
open and is not a measured 10x CPU slowdown. DOS is provided next for the
requested comparison; this work does not claim to fix NES speed or the old
photographed crashes.

## Matched downloads and installation

- [Current generic firmware](../../firmware/README.md): V1.1.1.
- [NESVM](../../vms/NESVM/README.md): ABI 2 launcher, engine and authorized Crossbow demo.
- [DOSVM](../../vms/DOSVM/README.md): ABI 2 launcher, engine, BIOS and fresh-disk ZIP.
- [DOS engine-only update](../../vms/DOSVM/engine.mvm): replace this file for the GRAPHSET fix; no disk or firmware change.

Install V1.1.1 and the matching ABI 2 packages together. Replace the earlier
NES client and engine as well; preserve private ROMs. For existing DOS data,
back up and copy `/DOSVM/DOSVM.IMG` and `/DOSVM/D/` into `/VMS/DOSVM/` following
the DOS guide. Never extract the fresh disk over a working image. Each ZIP's
members are reopened and checked against the verified source bytes; package
checksums are in `vms/<ID>/checksums.json`. Historical release kits are untouched.

Only one VM is loaded per boot. NES and DOS may both be installed on SD but
never have to fit in RAM together. Reset returns to the GUI; there is no
unload/reconstruct-GUI requirement. Finish storage writes before resetting.

## Measured RAM layout

The reset-time host profile is 192 KiB ITCM / 320 KiB DTCM within RAM1. Module
code/constants, control state, BIOS/device backing, render buffers and storage
support stay in RAM1. The full 524,288-byte RAM2 is guest-machine memory.

| Region | Address / budget |
| --- | --- |
| Generic host code | `0x00000000`, 96 KiB ceiling |
| Selected module code/constants | `0x00018000`, 96 KiB |
| Host state and 16 KiB heap | Below `0x20014000` |
| Selected module data/BSS/support | `0x20014000`, 192 KiB |
| Shared execution/interrupt stack | `0x20044000`, 48 KiB |
| Selected module guest RAM | `0x20200000`, 512 KiB |

| Linked image | Code/constants bytes | Module data + BSS bytes | Remaining module workspace |
| --- | ---: | ---: | ---: |
| NES | 75,108 | 3,168 | 193,440 |
| DOS (GRAPHSET fix) | 87,760 | 8,928 | 187,680 |

The generic host links 87,112 bytes of ITCM code and 51,488 bytes of static
RAM1 variables, with zero static RAM2 allocation. The GUI's linked stack
headroom is 30,492 bytes; the reset-time VM host reserves 49,152 bytes. These
are linked reservations, not physical stack high-water measurements.

DOS uses all RAM2 for 512 KiB conventional memory. Its compact RAM1 hardware
backing includes 64 KiB F000 BIOS/register storage, 32 KiB CGA/Tandy video RAM,
two 4 KiB BIOS text shadows and low 4 KiB I/O ports. Unsupported expansion
addresses do not alias implemented memory. No EMS/XMS, Hercules or arbitrary
expansion-hardware support is claimed. NES puts CPU/PPU guest bytes and ROM/CHR
backing in RAM2; its control and presentation state are in RAM1.

## Implemented and verified

Both engines are independent ARM modules, with no engine symbols in either
firmware image. The host supplies generic file/read/write/directory/metadata,
clock, cooperative yield, packet/ACK, input and typed-error services. DOS
filesystem policy, CPU, graphics, sound and keyboard logic live in its module.
`isrPHI2` was not changed. SD and module work remain outside the ISR path.

The final source-locked verification passed:

- Actual DOS module boots FreeDOS with 512 KiB, accepts input, writes and
  re-reads C:/D:, and executes a COM test through Tandy modes 08/09 back to text.
  This run produced 1,616 packets, 2,575 written bytes and 10 flushes; memory
  guards and pending-packet immutability passed.
- Existing DOS video/Boulder/Tandy, keyboard/timer and PC-speaker/Tandy
  three-voice checks pass; 12 packet-retry fault cases pass.
- Actual NES module passes 40-ROM picker navigation, 17-row paging, a 77-cell
  selection update without blanking, exact direct-game selection, immutable
  pending frames and 120 presented Crossbow frames. Portable core: 164,369 checks.
- Generic host storage tests cover writes, flush, truncate, rename, list,
  space, 24 handles, invalid spans/flags/paths and injected failures. Registry
  and malformed image/integrity rejection tests pass.
- Both C64 clients pass reset/START/bounded-no-host timeout in VICE PAL/NTSC;
  30 focused assembled GUI checks pass.
- ARM link checks enforce code/data bounds, zero host RAM2 state and no
  unresolved module imports/constructors. A separate diagnostic ARM module links.

Evidence: generated `build/vm-test/verification.json`, `verification.log`,
`build-inputs.json`, linked ELF/maps and `MODULAR-VM-TEST-V1.1.1.zip`. These are
host/build/emulator results; none proves actual C64 speed or sustained gameplay.

## GRAPHSET D: save regression — September 4, 2026

The user's exact GRAPHSET.EXE (SHA-256
`e181585a0fc3f79bedd494d59fc0e92393eb5da93038ea14de351e070b588fcc`)
reproduced error 32 when opening GACARD.DTA after creating it. Inspection of
the executable confirmed INT 21h/3Ch followed by 3D01h without an intervening
close. The redirector incorrectly treated compatibility mode as deny-all even
for a second compatibility open by the same PSP. The folder adapter duplicated
that owner-blind check.

The fix centralizes sharing checks in the PSP-aware redirector and permits
only the same-process, both-compatibility exception. Explicit denies and the
existing different-PSP rules remain checked. This follows the same-process
compatibility behavior documented under Function 3Dh in Microsoft's
[MS-DOS Encyclopedia](https://www.pcjs.org/documents/books/mspl13/msdos/encyclopedia/section5/).
Create/truncate is also flushed before returning the handle: the bundled
SdFat FatFile implementation retains a dirty zero-length directory entry
after O_TRUNC until sync, which must not later overwrite another handle's save.

Focused evidence:

- Unit reproduction failed before the fix; 450 access/share/PSP combinations
  and the create/reopen/write/termination regression pass after the fix.
- The actual module boots FreeDOS and executes an original COM reproduction
  20 times without handle leaks, plus a failed-create-flush cleanup test.
- The supplied GRAPHSET saves Tandy on C: and Tandy/CGA/Tandy on D:. Each save
  is exactly one byte, with guest readback. Private game files are not packaged.
- Existing C:/D: writes, Tandy modes 08/09, RAM guards and immutable packets pass.
- ARM code is 87,760 bytes; static data/BSS is unchanged at 8,928 bytes, with
  187,680 bytes available for module workspace. All 524,288 RAM2 bytes remain
  guest RAM. MVM header/integrity/ABI tests pass.

Reproduce with `node scripts/build-vm-test.mjs dos-module`, then
`node scripts/verify-dosvm.mjs <private-path-to-GRAPHSET.EXE>` (omit the path
for the redistributable tests). Evidence is in
`build/dosvm/dos-save-verification.json`. `scripts/publish-dosvm.mjs` stages
the verified engine and fresh-install ZIP without rebuilding/publishing
firmware or changing the C: image/D: files. H: was not modified.

Physical acceptance remains pending: replace only `/VMS/DOSVM/engine.mvm`,
run GRAPHSET in D:\\MM, choose 3, verify no DISK ERROR, then restart DOS and
confirm the game still uses Tandy. Firmware V1.1.1 and the client are unchanged.

## Next physical gates, in priority order

1. Install/update V1.1.1, verify About, launch DOSVM and reach `C:\>`; reset back
   to the GUI. Record PAL/NTSC and photograph the full diagnostic page on failure.
2. Run `DIR`, `DIR D:\`, `MEM`, `PCTONE`, then Boulder and a known working game.
   Compare input response, scrolling, screen completion and gameplay speed to
   the older DOS baseline. Check held/released and simultaneous keys.
3. Test Tandy modes 08/09, sharp toggle and three-voice sound on hardware;
   write/save to both drives, finish the command, reset and verify persistence.
4. Retest NES with the ABI 2 package. Measure emulated frame production, host
   packet service, ACK time and C64 drawing separately before optimizing the
   observed scanline-block updates. Preserve CPU/PPU/APU timing correctness.
5. Complete long-running transport, memory/stack high-water and reset/power
   stress. Confirm old field-crash causes separately. Then resume AGI/Doom
   extraction; flash/XIP profiles remain future work.
