# TeensyROM firmware V1.0.17 / DOSVM R20 validation

V1.0.17 pairs DOSVM R20 with a complete 80-column monochrome console. DOS
text mode now presents all 80 by 25 guest characters by packing two 4 by 8
glyphs into each C64 hires cell. The font is derived from mist64/80columns at
commit ece6df1afc598de385e1375d020973a4f02d755e. CGA graphics, including Might
and Magic and Boulder, still use their existing game-selected video modes.

The console follows the guest cursor position and visibility state and draws a
blinking underline cursor on either half of a packed cell. FreeDOS startup now
selects BIOS mode 3 through CGA80.COM. CGA40.COM remains available for software
that needs a manual 40-column text mode.

The firmware displays a short Mean Hamster BIOS screen before executing guest
instructions or reading the disk. It checks all 512 KiB of direct guest RAM,
reports `Memory Test: 512K OK`, identifies CGA 80 by 25 monochrome video, says
`Booting drive C:`, holds that screen for about 0.8 seconds, and sends a brief
startup tone through the PC-speaker/SID path.

Ctrl+A, Ctrl+B, Ctrl+C and the remaining tested keyboard press and release
sequences pass the host input checks. The physical R19 failure captured after
Ctrl+C was a transport packet CRC mismatch rather than a trapped Ctrl+C key.
R20 waits two complete C64 raster frames after the firmware reports quiet
status before the exceptional recovery reread. Normal acknowledged packets do
not take this delay. This hardens the observed retry boundary; the underlying
electrical cause of the one differing byte remains unproven.

The complete DOSVM build and acceptance suite passed. It covered the 512 KiB
direct RAM2 guest mapping; 20 MiB writable FAT16 C: drive; read/write SD-folder
D: redirector; repeated DIR commands without memory loss; FreeDOS MEM and
XCOPY; Ctrl key release; Boulder title, Shift start, arrow and joystick
movement; CGA mode changes and scrolling; PC-speaker output; and integrated
firmware operation without PSRAM.

The firmware simulation acknowledged 240 complete BIOS startup screens before
guest execution. The C64 wire replay passed 297 packets and 105 hires frames,
rendered all 2,000 console characters in 1,000 packed cells, and returned a
`C:\>` prompt. The graphics replay passed 1,114 packets, 302 multicolour
frames with 68 distinct images, five complete hidden replacements, and 429 SID
frames.

The final linked firmware retains 18,336 bytes of MinimalBoot stack and 337,376
bytes of pre-DOS RAM2 heap. DOS receives 512 KiB of direct RAM2. The firmware
is 6,377,075 bytes, SHA-256
`20d0ac933ebb947cf0d5db13574e4fa329209cffcd283ac3cf1dc7d4444a1367`.
The R20 CRT is 24,688 bytes, SHA-256
`a70a6b0b3caeed0e3a81bb4114746fa3559a671911a3ef62f18b7bc599d43784`.
The fresh 20 MiB image has 20,013,056 free bytes and SHA-256
`0431864dd7cdc697088d5b99f79f8313f68a26b972aea24e8b8ffb6a60a5765b`.

These are deterministic build, host, firmware-simulation and C64-replay
results. Physical TeensyROM acceptance remains open for the exact V1.0.17/R20
pair, including Ctrl+C recovery, the POST hold and beep, cursor blink,
80-column readability, sustained Might and Magic play, and Sierra regression.
