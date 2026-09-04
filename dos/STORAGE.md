# DOSVM drives

R18 uses two writable drives. It does not load either drive into guest RAM.

| DOS drive | SD card location | How to add files |
| --- | --- | --- |
| C: | `/DOSVM/DOSVM.IMG` | Copy from D: while DOS runs |
| D: | `/DOSVM/D/` | Copy files into this ordinary folder on your PC |

The new C: image is exactly 20 MiB (20,971,520 bytes), containing an active
FAT16 partition. Its fresh filesystem has about 19 MiB available. D: uses
the SD card's available storage. The folder is named `D`, without a colon.
Firmware creates it if it is missing, and `C:\DOSDIR.COM` mounts it during boot.

For example, put `GAME.EXE` in the SD card's `DOSVM/D/GAMES/` folder, then:

```dos
D:
CD \GAMES
DIR
GAME
```

These operations write real files and directories:

```dos
MD D:\SAVES
COPY C:\README.TXT D:\SAVES\NOTE.TXT
TYPE D:\SAVES\NOTE.TXT
REN D:\SAVES\NOTE.TXT CHECK.TXT
COPY D:\SAVES\CHECK.TXT C:\CHECK.TXT
DEL D:\SAVES\CHECK.TXT
RD D:\SAVES
```

`MEM`, `XCOPY`, `MORE`, and `ATTRIB` are included in `C:\FREEDOS\BIN`, which
is on PATH. `COPY`, `MD`/`MKDIR`, `RD`/`RMDIR`, `DIR`, `TYPE`, `REN`, and `DEL`
are commands built into FreeCOM. The original FreeDOS utility directory is
preserved; this is not just a boot sector and shell.

Use DOS 8.3 names throughout D:, such as `GAMES`, `LEVEL1.DAT`, or
`BOULDER.EXE`. Long filenames are skipped rather than given ambiguous truncated
aliases. The current folder adapter supports up to 16 open files, 16 searches,
127-byte DOS paths, and 1,024 entries per searched directory. Normal file
contents, timestamps, directories, truncation, rename, deletion and disk-space
queries are supported. Bulk D: operations finish one DOS file call before the
VM yields, so a large copy can briefly delay display/input updates. Hidden/read-only attributes are reported; unsupported
attribute changes fail instead of pretending to persist. Large sparse writes
with gaps over 64 KiB are rejected. This drive is intended for DOS applications
using DOS file calls, not disk formatting or utilities that write raw sectors.

Finish saving or copying before pressing reset or removing power. Successful
file writes and closes flush to the SD card; a reset in the middle of a DOS
filesystem operation can still interrupt it. Keep backups of saves and the
C: image. Future kit images are fresh templates, so do not overwrite your
working image or D: folder when installing a newer firmware/CRT pair.

The FAT16 boot-sector source and its GPL license are included under
`dos/vendor/freedos-boot/`. The image manifest records hashes for the pinned
FreeDOS distribution, boot sector, FreeCOM, and every installed file. The
folder redirector is part of the native DOS source and has no PSRAM or heap
requirement after RAM2 takeover. Its resident DOS entry stub uses 272 bytes,
plus DOS's normal allocation bookkeeping.
