# DOSVM source and third-party notices

The independently compiled module source is maintained at:
https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/tree/main/vm/dos
Portable CPU, video, sound and redirector sources:
https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/tree/main/engine/native-dos

8086tiny by Adrian Cable, MIT licensed. The adapted CPU and BIOS derive from
revision c79ca2a34d96931d55ef724c815b289d0767ae3a. See LICENSE-8086tiny.txt;
the complete adapted BIOS assembly and CPU sources are in the repository above.

The fresh C: image is copied byte-for-byte from this project's existing FreeDOS
template, not built from a user's disk. Its contents/hashes are recorded in
source-image-manifest.json. FreeDOS components retain their upstream licenses;
the FAT16 boot-sector GPL text is included as LICENSE-FreeDOS-boot.txt.
Boot-sector source: repository dos/vendor/freedos-boot (boot.asm and magic.mac).
FreeDOS distributions and corresponding source: https://www.freedos.org/download/
FreeCOM source: https://github.com/FDOS/freecom
Kernel source: https://github.com/FDOS/kernel

FreeDOS Edit 0.9c (GNU GPL v2) comes from official package 20250530.1:
https://ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/latest/base/edit/20250530.1/edit.zip
The archive includes the corresponding source and license; pinned SHA-256:
244edc7f1aa4cd3680d9341dc67cac268df7a3e4910ab53a71272fb1925cf31f.

The 8x8 font is Daniel Hepper's public-domain font8x8_basic, derived from
Marcel Sondaar/IBM VGA fonts. The 4x8 font provenance is retained alongside
engine/native-dos/mpe5_font4x8.h. No new commercial game has been added to this kit.
