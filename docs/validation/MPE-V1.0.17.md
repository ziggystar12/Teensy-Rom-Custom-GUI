# TeensyROM firmware V1.0.17 / DOSVM R21 validation

V1.0.17 pairs with DOSVM R21 and its complete 80-column monochrome console.
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

## R20 physical failure and R21 receiver correction

The physical R20 test stopped at stage `03`, error `02`, with zero accepted
packets and controls `4D 33 54 50 04 02 00 01`. Commit `01` shows that the
V1.0.17 firmware had published packet 1. The receiver had entered exceptional
quiet recovery after an early read fault and then waited for two changes to a
raster-frame counter that is enabled only after the initial bitmap completes.

R21 keeps bootstrap recovery on the existing bounded immediate reread path.
Once the base display is complete and the frame counter is running, failed
reads retain command `04`, firmware quiet status `12`, two-frame settling, CRC
validation, and bounded rejection of persistent corruption. This is a CRT-only
change; the V1.0.17 firmware and the C:/D: drives are unchanged.

The executable 6510 recovery regression failed against the R20 logic with a
transient bad first-packet CRC and no ACK, reproducing the startup deadlock. It
passes with R21, along with normal packets, later transient commit/CRC/length
faults, persistent faults, a dropped quiet request, and an explicit firmware
error. Ctrl+A, Ctrl+B, Ctrl+C and the remaining tested keyboard press and
release sequences continue to pass.

The integrated C64 wire replay passed 297 firmware packets and 105 hires
frames, rendered all 2,000 console characters in 1,000 packed cells, and
returned a `C:\>` prompt. The actual R21 CRT also boots from C64 reset in VICE,
copies the generated receiver byte-for-byte, issues the M3TP start command, and
holds its diagnostic when no Teensy service exists.

The final linked V1.0.17 firmware retains 18,336 bytes of MinimalBoot stack and
337,376 bytes of pre-DOS RAM2 heap. DOS receives 512 KiB of direct RAM2. The
unchanged firmware is 6,377,075 bytes, SHA-256
`20d0ac933ebb947cf0d5db13574e4fa329209cffcd283ac3cf1dc7d4444a1367`.
The R21 CRT is 24,688 bytes, SHA-256
`2248cc098f979928dfa94f1b5a4f02f94bfec2adfd63610116f24677cc2466ee`.
The unchanged fresh 20 MiB image has 20,013,056 free bytes and SHA-256
`0431864dd7cdc697088d5b99f79f8313f68a26b972aea24e8b8ffb6a60a5765b`.

These are deterministic build, firmware-simulation, executable 6510, C64
replay, and VICE results. Physical TeensyROM acceptance remains open for the
exact V1.0.17/R21 pair, including cold startup, Ctrl+C recovery, the POST hold
and beep, cursor blink, 80-column readability, sustained Might and Magic play,
sharp CGA, Boulder scrolling, and Sierra regression.
