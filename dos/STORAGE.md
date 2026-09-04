# DOSVM drives

DOSVM uses two writable drives. It does not load either drive into guest RAM.

| DOS drive | SD card location | How to add files |
| --- | --- | --- |
| C: | `/DOSVM/DOSVM.IMG` | Copy from D: while DOS runs |
| D: | `/DOSVM/D/` | Copy files into this ordinary folder on your PC |

The supplied C: image is exactly 20 MiB (20,971,520 bytes), containing an active
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
are commands built into FreeCOM. The FreeDOS utility directory is preserved.

Use DOS 8.3 names throughout D:, such as `GAMES`, `LEVEL1.DAT`, or
`BOULDER.EXE`. Long filenames are skipped rather than given ambiguous truncated
aliases. The current folder adapter supports up to 16 open files, 16 searches,
127-byte DOS paths, and 1,024 entries per searched directory. Normal file
contents, timestamps, directories, truncation, rename, deletion and disk-space
queries are supported. Bulk D: operations finish one DOS file call before the
VM yields, so a large copy can briefly delay display/input updates.
Hidden/read-only attributes are reported; unsupported
attribute changes fail instead of pretending to persist. Large sparse writes
with gaps over 64 KiB are rejected. This drive is intended for DOS applications
using DOS file calls, not disk formatting or utilities that write raw sectors.

Finish saving or copying before pressing reset or removing power. Successful
file writes and closes flush to the SD card; a reset in the middle of a DOS
filesystem operation can still interrupt it. Keep backups of saves and the
C: image. Future kit images are fresh templates, so do not overwrite your
working image or D: folder when installing a newer firmware/CRT pair.

## Upgrading DOSVM

The root `DOSVM/` distribution includes a fresh C: template for new
installations. **Do not overwrite `/DOSVM/DOSVM.IMG` or replace `/DOSVM/D/`
when upgrading an existing installation.**

1. Back up the working image and D: folder.
2. If About is older than V1.0.19, install
   `DOSVM/firmware/MPE_Firmware-V1.0.19.hex`. Copy the paired R23
   `DOSVM/sd-card/DOSVM.CRT` to SD `/DOSVM.CRT`; users already on
   V1.0.19 do not need to reflash.
3. Copy only the supplied `DOSVM/sd-card/DOSVM/D/DOSVMUPD/` directory to
   SD `/DOSVM/D/DOSVMUPD/`. Leave other D: files and the C: image in place.
4. Launch DOSVM and run `D:\DOSVMUPD\UPDDOS` unless it was already run for R20.
5. Wait for completion, then reset and relaunch DOSVM.

The updater installs `AUTOEXEC.BAT`, `CONFIG.SYS` and `FDCONFIG.SYS` on C:.
It preserves the previous startup files as `AUTOEXEC.OLD`, `CONFIG.OLD` and
`FDCONFIG.OLD` only when those backups do not already exist. Review any custom
startup settings against those backups. Games and saves are left in place.
The updated startup sets PATH, retains standard DOS 80-column text mode and mounts D: quietly.

The FAT16 boot-sector source and its GPL license are included under
`dos/vendor/freedos-boot/`. The image manifest records hashes for the pinned
FreeDOS distribution, boot sector, FreeCOM, and every installed file. The
folder redirector is part of the native DOS source and has no PSRAM or heap
requirement after RAM2 takeover. Its resident DOS entry stub uses 272 bytes,
plus DOS's normal allocation bookkeeping.
