# MPE Firmware V1.0.14 / DOSVM R18 validation

R18 adds writable C: and D: drives while preserving 512 KiB guest RAM,
R17 quiet packet recovery, the GUI updater fix, and native Sierra support.
No physical cartridge was flashed or operated during these checks.

- C: is an exact 20 MiB FAT16 disk with valid FAT dates and a relocated MBR.
- Independent image parsing verifies 80 original FreeDOS files, MEM/XCOPY
  source hashes, FAT mirrors, cluster ownership and directory links.
- Real FreeDOS tests cover MEM/XCOPY, a 20,037-byte binary C/D round trip,
  the shipped Boulder copy, directories, rename/delete, seek/truncate,
  commit, duplicate handles, process exit, errors, and restart persistence.
- The integrated production folder adapter passes 12 drive/persistence
  checks; Boulder runs from D: with 3,632 CGA frames and 80 audible frames.
- Transport tests cover 4,096 immutable packets, 64 quiet retries and
  interleaved input, plus reset ownership and deferred CPU failure.
- C64 replay passes 193 text packets and 709 graphics packets, including
  234 multicolor frames and 319 exact SID register frames.
- The separate Sierra run passes 7,244 intro packets, 862 gameplay frames,
  room 2 progression, sprite/bitmap paths and save compatibility.
- Combined HEX/link audit verifies the selected GUI and compiled inputs,
  resident bus handlers, memory limits and matching embedded firmware images.

MinimalBoot retains 21,344 stack bytes and 337,376 pre-DOS heap bytes.
Folder state lives in the spare RAM1 cartridge buffer; the ARM folder adapter
is 4,276 bytes. No guest RAM is consumed by its native state. D: operations
complete one DOS call before yielding (up to 100 storage operations observed
for the Boulder copy), so C:'s four-sector cap is not a D: bulk-I/O guarantee.

The fresh image has 20,014,080 bytes free. See `dos/image-manifest.json` for
all installed-file hashes and source pins; `dos/STORAGE.md` documents use and
8.3 filename limits. Physical SD persistence, speed and sustained gameplay
remain acceptance steps for this exact kit.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `MPE_Firmware-V1.0.14.hex` | 6,368,448 | `9d74cc91a1879370561c43c05cc45eb6d3d97c78aa3dfd28586adc80a5c861fd` |
| `DOSVM.CRT` | 24,688 | `8e134cbd42beb5b36841808c3a151173f1f451b9e4efd608ac56a4858c190704` |
| `DOSVM.IMG` | 20,971,520 | `92ae61d3f4e8ea59221ce43eaba9fd91056d18e61b34d995a0191216d8f6f4f4` |
