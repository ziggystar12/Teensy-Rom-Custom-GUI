# DOS original default plus explicit F5 — September 5, 2026

The user wanted F5 available to compare with the previously working Tandy
renderer, not removed by the temporary rollback. This module/client-only
update runs on the existing immutable V1.1.7 firmware.

## Behavior

Boot, CGA80 text and default graphics use the original converter and dirty
CELL/SID transport from `ef9cc9114fad`. `mpe5_video.cpp/.h` are unchanged.
In particular Tandy 09h defaults to 320-wide hires, not multicolor.

Commodore + Control + F5 explicitly selects the firmware's Enhanced-25
indexed-raster service. F1 restores the original DOS renderer. F7 retains the
original CGA Sharp toggle, or selects Sharp when leaving F5. Tandy 09h remains
hires either way. F3 is not added in this focused update. Ordinary function
keys remain guest input, and shortcut release tails are consumed by DOSVM.

The raw DOS snapshot envelope is retained; no NES-specific input protocol is
used. DOS keyboard matrix scratch moves from $02e0 to $02d8 to keep the shared
receiver's IRQ trampoline and state intact. The enhanced receiver is present
but inactive by default. CELL return restores VIC bank $4000 and the normal IRQ.

## Memory and frame ownership

The original 42,893-byte video arena is lent to the shared service only while
F5 is active, with 36 KiB available for its two exact destination-bank caches.
During this time the original video observer is disabled; live guest VRAM
remains in DosMemory. A consumed descriptor/palette/generation stays frozen
through upload and resume/flip ACK while the emulated CPU continues running.

F1/F7 and text-mode return wait for that generation to complete before
reclaiming the arena. The original renderer is then reset, its 32 KiB VRAM
mirror rebuilt from current guest memory, and a complete CELL replacement
sent. No frame cancellation is invented for the V1.1.7 ABI: a physically
stalled link may still require hardware reset. This is not proof of the root
electrical/timing cause of the earlier static.

ARM code: 88,984 bytes of the 98,304-byte window. Static RAM1: 9,088 bytes;
workspace: 187,520 bytes available, 175,464 used. Guest RAM2 stays 524,288 bytes.

## Verification and handoff

`MPE_VM_TEST_OUT=build/dos-f5` selects the focused build directory.
Build with `node scripts/build-vm-test.mjs dos-module`; verify with
`node scripts/verify-dosvm.mjs --f5-opt-in`; package with
`node scripts/publish-dos-restoration.mjs --f5-opt-in`.

Checks cover real FreeDOS boot and C:/D: saves, both Tandy COM modes, zero
indexed configuration/DMA before selection, real firmware F5 conversion and
PAL/NTSC bank streaming, immutable output during guest VRAM writes, and F1
selected while Busy. The default wire trace is replayed through the generated
6510 client with all 1,000 Tandy cells compared, then enhanced bank-1 return
to the original bank/IRQ is checked. Input/recovery tests and PAL/NTSC VICE
boot and enhanced raster tests also run; raster tests enable DOS key sampling.

The update ZIP contains neither C: disk images nor D: games/saves. Firmware,
NES, GB and AGI binaries are not rebuilt or replaced. Current artifact hashes
are in `vms/DOSVM/checksums.json`.

Physical acceptance remains open: boot to C:, start Monkey Island in Tandy
without shortcuts, select Commodore + Control + F5, compare image/cadence,
then F1/F7 and return to the prompt. Report any static, black flashes or slow
input/sound. Host/emulator evidence does not establish physical DMA quality.
