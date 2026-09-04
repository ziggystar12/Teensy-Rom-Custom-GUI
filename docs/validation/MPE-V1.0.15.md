# TeensyROM firmware V1.0.15 / DOSVM R19 validation

TeensyROM now documents its GUI, MHS Power Engine and DOSVM as three
components. The user has confirmed DOSVM working. This update removes demo
startup text, adds a BIOS page, and fixes CGA scrolling publication.

The Boulder reproduction moves one cell down, then holds Right. The old code
issued two hidden replacements during ten CRTC origin changes. The fix keeps
the picture visible; only actual bitmap-format transitions hide it. C64 CPU
replay verifies no display-disable writes or hidden state through the entire
198-packet scrolling interval (origin 0 to 16). The full graphics replay passes
947 packets, 300 multicolor frames and 386 exact SID frames. Text replay passes
247 packets and 60 hires frames.

The BIOS page displays Mean Hamster BIOS (C) 2026, 512K OK, and Booting drive C:.
All 512 KiB of reset guest RAM is read back before OK appears; a corrupted last
byte rejects startup. Five integrated boots verify the complete page is
acknowledged before guest instructions or guest disk-sector reads begin.
There is no fixed wait or simulated POST counter. FreeDOS then boots with
quiet startup configuration and its normal C:\> prompt.

D:\DOSVMUPD\UPDDOS backs up AUTOEXEC/CONFIG/FDCONFIG as .OLD files once and
updates only those files inside C:. Real FreeDOS checks confirm backups,
installed content, and C:/D: data persistence. It never replaces the image.
The integrated run passes 6,828 DOS packets, 64 quiet retries and 4,096 pending
packets protected against incorrect ACKs, plus writable drives, MEM/XCOPY,
keyboard, sound and Boulder from D:. The separate Sierra run passes 7,244
intro packets, 862 gameplay frames and progression to room 2.

The combined-image audit passes source/GUI hashes, embedded images, ITCM bus
handlers and memory guards. MinimalBoot retains 21,344 stack bytes and
337,376 pre-DOS heap bytes; DOS retains all 512 KiB of direct guest RAM.
Physical confirmation of this scrolling correction and BIOS presentation is
separate from these host checks. See dos/HARDWARE-TEST.md for the same route.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `MPE_Firmware-V1.0.15.hex` | 6,368,448 | `2553d57eed72cd0d491c11ee8c96df39c068cd8291efe1170cb07e29f73dc5d7` |
| `DOSVM.CRT` | 24,688 | `ec13972c56ab18b04fd2a4ebe9b5ba80cf8601252f7a6b9b98f4ffa643d74f21` |
| `DOSVM.IMG` | 20,971,520 | `3bb86925b3175188ac67f740bca9541a5d08a9925011874af4e321cfc4c21a81` |
