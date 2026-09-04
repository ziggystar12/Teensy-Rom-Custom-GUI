# TeensyROM firmware V1.0.17 / DOSVM R23 validation

V1.0.17 pairs with DOSVM R23 and its complete 80-column monochrome console.
DOS text mode presents all 80 by 25 guest characters by packing two 4 by 8
glyphs into each C64 hires cell. The font is derived from mist64/80columns at
commit ece6df1afc598de385e1375d020973a4f02d755e. CGA graphics, including Might
and Magic and Boulder, still use their existing game-selected video modes.

The console follows the guest cursor position and visibility state and draws a
blinking underline cursor on either half of a packed cell. FreeDOS startup
selects BIOS mode 3 through CGA80.COM. CGA40.COM remains available for software
that needs a manual 40-column text mode.

The firmware displays a short Mean Hamster BIOS screen before executing guest
instructions or reading the disk. It checks all 512 KiB of direct guest RAM,
reports `Memory Test: 512K OK`, identifies CGA 80 by 25 monochrome video, says
`Booting drive C:`, holds that screen for about 0.8 seconds, and sends a brief
startup tone through the PC-speaker/SID path.

## R20/R21 physical failures and R22 receiver correction

The physical R20 test stopped at stage `03`, error `02`, with zero accepted
packets and controls `4D 33 54 50 04 02 00 01`. Commit `01` shows that the
V1.0.17 firmware had published packet 1. The receiver had entered exceptional
quiet recovery after an early read fault and then waited for two changes to a
raster-frame counter that is enabled only after the initial bitmap completes.

The physical R21 test also stopped before accepting a packet, this time at
stage `03`, error `0C` (**UNSTABLE PACKET COMMIT**), and packet count `0000`.
R21's bounded bootstrap rereads happened immediately, so every attempt could
still observe the unsettled IO2 commit byte.

R22 requests command `04` for failed bootstrap reads as well as later reads.
After firmware reports quiet status `12`, the receiver measures two frames
directly from live VIC register `$D012`; this raster is available before the
terminal enables its interrupt-driven frame counter. It then rereads the same
packet. CRC validation and bounded rejection of persistent corruption remain.
This is a CRT-only change; V1.0.17 firmware and the C:/D: drives are unchanged.

The executable 6510 recovery regression now holds the first packet corrupt
until a quiet request is observed. R21 fails it with zero quiet requests and
no accepted packet, matching the physical failure; R22 passes. Normal packets,
later transient commit/CRC/length faults, persistent faults, a dropped quiet
request, and an explicit firmware error also pass. Ctrl+A, Ctrl+B, Ctrl+C and
the remaining tested keyboard press and release sequences continue to pass.

The integrated C64 wire replay passed 297 firmware packets and 105 hires
frames, rendered all 2,000 console characters in 1,000 packed cells, and
returned a correctly drawn `C:\>` prompt. R23 corrects V1.0.17's C64
pound-sign glyph substitution in the DOS-only CRT receiver. Its graphics
replay remains byte-identical to the known-good Boulder output. The actual R23
CRT also boots from C64 reset in VICE,
copies the generated receiver byte-for-byte, issues the M3TP start command, and
holds its diagnostic when no Teensy service exists.

The final linked V1.0.17 firmware retains 18,336 bytes of MinimalBoot stack and
337,376 bytes of pre-DOS RAM2 heap. DOS receives 512 KiB of direct RAM2. The
unchanged firmware is 6,377,075 bytes, SHA-256
`20d0ac933ebb947cf0d5db13574e4fa329209cffcd283ac3cf1dc7d4444a1367`.
The R23 CRT is 24,688 bytes, SHA-256
`3309f977e2c99131201685e44e7b552da67e2f6aba03d7839d41189eeb6a65af`.
The unchanged fresh 20 MiB image has 20,013,056 free bytes and SHA-256
`0431864dd7cdc697088d5b99f79f8313f68a26b972aea24e8b8ffb6a60a5765b`.

These are deterministic build, firmware-simulation, executable 6510, C64
replay, and VICE results. The user physically confirmed the V1.0.17/R22 cold
start and successful Boulder and Might and Magic play. R23 retains that code
and awaits only a visual check that DOS paths now show a backslash. Sharp CGA,
Boulder scrolling, and Sierra regression remain separate physical checks.
