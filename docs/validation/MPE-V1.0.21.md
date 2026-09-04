# TeensyROM firmware V1.0.21 / native29 validation

V1.0.21 is the first combined image that contains the MHS desktop and Power
Engine together with the DOSVM R24, NESVM, and DOOMVM firmware services. DOSVM
also includes Tandy 16-colour video and three-voice sound in this build.

## Release identity

| Item | Recorded value |
| --- | --- |
| Firmware | `releases/native29/MPE_Firmware-V1.0.21.hex` |
| Firmware size | 8,145,548 bytes |
| Firmware SHA-256 | `4261dcd708872c46f6e50547065bb91c98347807538445894c794d1da6af3c76` |
| Official restore SHA-256 | `575ab4e237b1c9d5539e8d56248490dd471c6e368d2c98fd66311dddb65252bf` |
| GUI source commit | `51acc24dba4f5fe1c63f69abd412b29c44da73e8` |
| GUI snapshot | `gui/selected-v1.0.21` |
| GUI snapshot digest | `d2da5fcda3fedcc757371a43c27e620968ec998b149f98893dd6489921a50274` |
| Integration patches | 50 |
| Native inputs | 9 game, 21 DOS, 12 NES, 10 Doom, 1 shared-runtime |

The linked MinimalBoot image retains a 27,648-byte stack reserve and a
337,376-byte normal RAM2 heap reserve. DOSVM takes direct reset-only ownership
of 512 KiB RAM2 while running. DOOMVM uses a 305,344-byte linked RAM2 runtime
inside that mutually exclusive overlay and requires exactly 8 MiB of external
PSRAM.

## DOSVM R24 and Tandy

The end-to-end DOS build passed real FreeDOS cold boot and restart, dirty-memory
startup, writable C: and SD-folder D:, repeated DIR memory stability,
VER/SETUP/VER, the FreeDOS Edit package, keyboard and port-2 joystick input,
Ctrl+Alt+Delete, Boulder controls, CGA, sharp CGA, Tandy modes 08h/09h, BIOS
timer, PC speaker, three Tandy PSG voices, packet recovery, bounded foreground
latency, and executable C64 text and graphics replay.

The integrated harness completed 7,954 DOS packets, 4,965 display frames, 1,107
keyboard events, 4,010 Boulder CGA frames, and 293 audible SID frames. It ACKed
336 complete BIOS startup screens before guest instructions or disk reads and
completed 18 real C:/D: persistence and folder operations. The Black Cauldron
M4G2 regression also reached two native Sierra frames in the same firmware.

| DOSVM artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| R24 cartridge | 24,688 | `42fd92a7c7b62c9d15170f3a675d399147eb9706fd933cfe373ea754bb9e2e3a` |
| Fresh writable FAT16 image | 20,971,520 | `2bbacc7db1ce4fa06cc959a07fad56dedf911fb5b1fbcd7e3c272bca39540816` |
| Black Cauldron M4G2 fixture | 476,128 | `760add629c52f060cfada82024a9da56f025b5bd9ef9bfc80175a7b8227afdf6` |

The fresh C: image has 19,888,128 bytes free. FreeDOS reports 376 KiB of free
conventional memory and a 373 KiB largest executable program. Existing users
should retain their C: image and D: folder, replace the CRT, and run
`D:\DOSVMUPD\UPDDOS` once.

## NESVM

The NES core passed 164,369 host checks plus Cortex-M7 type/layout compilation,
ROM-policy audit, terminal/cartridge validation, VICE boot, and exact one-demo
SD packaging. The resulting `NESVM.CRT` is 24,688 bytes with SHA-256
`80b868fe0bde2559a0b59977812c39664ab6e673f4f390050472803393828396`.
The current software support is Mapper 0 plus the authorized Mapper-11 Crossbow
demo, complete-frame 256x240 presentation, keyboard/port-2 input, and basic SID
mapping.

## DOOMVM

The pinned native Doom source passed its source lock, host runtime/session/video,
Cortex-M7 session/video, cartridge layout, and a native E1M1 run through 493
gametics. `DOOMVM.CRT` is 24,688 bytes with SHA-256
`7ea0daee4720847e155e4631920a8688fd41f0080b6a2cd4b3d181f23849c6c9`.
The user-supplied WAD remains outside the firmware and Git release. The pinned
upstream subtree still has unresolved per-file license coverage, so its release
manifest deliberately records `publicationReady: false`.

## Acceptance boundary

These results cover deterministic inputs, firmware compilation and linking,
host execution, C64 replay, package integrity, and memory-layout guards. They
do not prove the new combined V1.0.21 image, Tandy output, NES gameplay, Doom
startup, or sustained operation on physical TeensyROM hardware. Those remain
the final hardware acceptance checks for this exact firmware and cartridge set.
