#!/usr/bin/env python3
"""Build the writable 20 MiB FreeDOS C: disk for the native x86 VM.

The builder extracts the official FreeDOS 1.44 MiB boot disk, retains its system files, and replaces only the configuration files plus the
small test payload. The original FreeDOS directory tree is migrated to FAT16,
including MEM and XCOPY. No host filesystem or image-editor dependency is used.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path


BOOT_IMAGE_ENTRY = "144m/x86BOOT.img"
FREEDOS_SHA256 = "ae0a074f3688da1d247b946575142164a47b00d4d8cce51aa7bf8c1aaa9a55c6"
FAT12_EOC = 0xFFF
HARD_DISK_START_SECTOR = 63
HARD_DISK_SECTORS_PER_TRACK = 63
HARD_DISK_HEADS = 1
HARD_DISK_BYTES = 20 * 1024 * 1024
FAT16_BOOT_SHA256 = "c78d072846e03ae940d9c4904c3805df577657f6ed9a286986a8278fe496f71d"
FREECOM_COMMAND_SHA256 = "ae6aee6b18360c5408e5293fe906ab9b333158a32b50d604ca32177711aab768"
FREECOM_KSSF_SHA256 = "ab26a437879069efb378636f96524fa90bc0f58d3150f0f456486963e5052a76"
FREEDOS_EDIT_SHA256 = "e972ca9f5b25e97e2959057809a1f640123649c3da76971ec829ced6cbbe1ced"
FREEDOS_EDIT_HELP_SHA256 = "9c90eac60b8065d1d12f13af679b7895512eb76d3007e107e755f68f5b9d2265"

# CGA80 selects standard BIOS mode 3 (80x25 colour text). Keep this proven
# mode switch minimal; AUTOEXEC immediately repaints the POST text it clears.
CGA80_COM = bytes((0xB8, 0x03, 0x00, 0xCD, 0x10, 0xB8, 0x00, 0x4C, 0xCD, 0x21))
# Retain a manual 40-column compatibility helper for programs which need it.
CGA40_COM = bytes((0xB8, 0x01, 0x00, 0xCD, 0x10, 0xB8, 0x00, 0x4C, 0xCD, 0x21))
# Real 8086 PIT/speaker writes: approximately 1 kHz, bounded delay, silence,
# then DOS terminate. Preserve the unrelated port 61h control bits.
PCTONE_COM = bytes.fromhex(
    "B0 B6 E6 43 "  # Channel 2, low/high reload, mode 3, binary.
    "B0 A9 E6 42 B0 04 E6 42 "  # 1193 = 04A9h; PIT clock / 1193 ~= 1 kHz.
    "E4 61 0C 03 E6 61 "  # Enable PIT gate and speaker.
    "BA 08 00 "  # Eight bounded delay loops; duration follows guest speed.
    "B9 FF FF E2 FE 4A 75 F8 "
    "E4 61 24 FC E6 61 "  # Disable speaker, retaining other control bits.
    "B8 00 4C CD 21"  # Exit through DOS with status zero.
)
AUTOEXEC_BAT = (
    "@ECHO OFF\r\n"
    "PATH C:\\;C:\\FREEDOS\\BIN\r\n"
    "CGA80\r\n"
    "ECHO Mean Hamster BIOS (C) 2026\r\n"
    "ECHO TeensyROM DOSVM\r\n"
    "ECHO.\r\n"
    "ECHO CPU: 8086 compatible\r\n"
    "ECHO Memory Test: 512K OK\r\n"
    "ECHO Video: CGA 80 x 25 monochrome\r\n"
    "ECHO.\r\n"
    "ECHO Booting drive C:\r\n"
    "PROMPT $p$g\r\n"
).encode("ascii")
CONFIG_SYS = (
    "SWITCHES=/F\r\n"
    "DOS=HIGH\r\n"
    "FILES=20\r\n"
    "BUFFERS=12\r\n"
    "LASTDRIVE=Z\r\n"
    "SHELL=C:\\COMMAND.COM /E:256 /P\r\n"
).encode("ascii")
README_TXT = (
    "Mean Hamster DOSVM\r\n"
    "\r\n"
    "At the C:\\ prompt, type:\r\n"
    "  DIR\r\n"
    "  VER\r\n"
    "  EDIT filename.txt - FreeDOS text editor\r\n"
    "  PCTONE   - PC speaker tone, then return to DOS\r\n"
    "  BOULDER  - Boulder Dash\r\n"
    "\r\n"
    "C: is a writable 20 MiB disk image.\r\n"
    "D: shares the SD card DOSVM/D folder when DOSDIR is installed.\r\n"
    "DOS commands use the 80 x 25 monochrome console. CGA games retain\r\n"
    "their normal graphics modes.\r\n"
    "EDIT, MEM and XCOPY are in FREEDOS/BIN; COPY, MD and RD are shell commands.\r\n"
    "PCTONE programs the PC PIT for an approximately 1 kHz tone,\r\n"
    "then switches the speaker off and returns to the prompt.\r\n"
    "BOULDER: Space skips the intro, then hold Shift to start.\r\n"
    "Cursor keys move, Shift grabs, Space pauses during play.\r\n"
    "C64 Shift+cursor selects Up/Left. Both Shift keys work.\r\n"
    "Port 2 joystick directions act as cursor keys; fire is Shift.\r\n"
    "This is keyboard translation, not a PC joystick.\r\n"
    "Held keys and releases include Shift/Ctrl/Alt.\r\n"
    "The BIOS checks initialized RAM before booting C:.\r\n"
).encode("ascii")


def startup_autoexec(redirector: bool) -> bytes:
    return AUTOEXEC_BAT.replace(b"PROMPT $p$g\r\n", b"DOSDIR >NUL\r\nPROMPT $p$g\r\n") if redirector else AUTOEXEC_BAT


def startup_upgrade_payloads(redirector: bool, edit: bytes, edit_help: bytes) -> dict[str, bytes]:
    """An explicit in-DOS update; never replace the user's writable image."""
    lines = ["@ECHO OFF", "ECHO Updating DOS startup files..."]
    updated = ("AUTOEXEC.BAT", "CONFIG.SYS", "FDCONFIG.SYS", "CGA80.COM")
    for index, name in enumerate(updated):
        backup = name.split(".")[0] + ".OLD"
        lines.extend((f"IF EXIST C:\\{backup} GOTO SAVED{index}",
                      f"IF NOT EXIST C:\\{name} GOTO SAVED{index}",
                      f"COPY C:\\{name} C:\\{backup} >NUL",
                      "IF ERRORLEVEL 1 GOTO FAILED", f":SAVED{index}"))
    for name in updated:
        lines.extend((f"COPY /Y D:\\DOSVMUPD\\{name} C:\\{name} >NUL",
                      "IF ERRORLEVEL 1 GOTO FAILED"))
    lines.extend(("IF NOT EXIST C:\\FREEDOS\\BIN\\NUL MD C:\\FREEDOS\\BIN >NUL",
                  "COPY /Y D:\\DOSVMUPD\\EDIT.EXE C:\\FREEDOS\\BIN\\EDIT.EXE >NUL",
                  "IF ERRORLEVEL 1 GOTO FAILED",
                  "COPY /Y D:\\DOSVMUPD\\EDIT.HLP C:\\FREEDOS\\BIN\\EDIT.HLP >NUL",
                  "IF ERRORLEVEL 1 GOTO FAILED"))
    lines.extend(("ECHO Startup updated. Reboot to use it.", "GOTO DONE", ":FAILED",
                  "ECHO Update failed. Original backups are in C:\\*.OLD", ":DONE"))
    return {"AUTOEXEC.BAT": startup_autoexec(redirector), "CONFIG.SYS": CONFIG_SYS,
            "FDCONFIG.SYS": CONFIG_SYS, "CGA80.COM": CGA80_COM,
            "EDIT.EXE": edit, "EDIT.HLP": edit_help,
            "UPDDOS.BAT": ("\r\n".join(lines) + "\r\n").encode("ascii")}


def write_startup_upgrade(directory: Path, redirector: bool, edit: bytes,
                          edit_help: bytes) -> dict[str, dict]:
    directory.mkdir(parents=True, exist_ok=True)
    payloads = startup_upgrade_payloads(redirector, edit, edit_help)
    for name, data in payloads.items():
        (directory / name).write_bytes(data)
    return {name: {"bytes": len(data), "sha256": sha256(data)} for name, data in payloads.items()}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def name83(name: str) -> bytes:
    path = Path(name)
    stem = path.stem.upper()
    suffix = path.suffix[1:].upper()
    if not stem or len(stem) > 8 or len(suffix) > 3:
        raise ValueError(f"not a DOS 8.3 name: {name}")
    try:
        return stem.encode("ascii").ljust(8, b" ") + suffix.encode("ascii").ljust(3, b" ")
    except UnicodeEncodeError as error:
        raise ValueError(f"not an ASCII DOS name: {name}") from error


@dataclass(frozen=True)
class DirectoryEntry:
    name: str
    offset: int
    first_cluster: int
    size: int


class Fat12Root:
    fat_bits = 12

    def __init__(self, image: bytearray) -> None:
        self.image = image
        if len(image) < 512 or image[510:512] != b"\x55\xAA":
            raise ValueError("not a bootable FAT image")
        self.bytes_per_sector = struct.unpack_from("<H", image, 11)[0]
        self.sectors_per_cluster = image[13]
        self.reserved_sectors = struct.unpack_from("<H", image, 14)[0]
        self.fat_count = image[16]
        self.root_entries = struct.unpack_from("<H", image, 17)[0]
        total16 = struct.unpack_from("<H", image, 19)[0]
        total32 = struct.unpack_from("<I", image, 32)[0]
        self.total_sectors = total16 or total32
        self.sectors_per_fat = struct.unpack_from("<H", image, 22)[0]
        if (
            self.bytes_per_sector != 512
            or self.sectors_per_cluster == 0
            or self.fat_count == 0
            or self.root_entries == 0
            or self.sectors_per_fat == 0
            or self.total_sectors * self.bytes_per_sector != len(image)
        ):
            raise ValueError("unsupported FAT12 geometry")
        self.fat_offset = self.reserved_sectors * self.bytes_per_sector
        self.fat_bytes = self.sectors_per_fat * self.bytes_per_sector
        self.root_offset = self.fat_offset + self.fat_count * self.fat_bytes
        self.root_bytes = self.root_entries * 32
        self.data_offset = self.root_offset + self.root_bytes
        self.cluster_bytes = self.sectors_per_cluster * self.bytes_per_sector
        self.cluster_count = (len(image) - self.data_offset) // self.cluster_bytes
        if self.fat_bits == 12 and self.cluster_count >= 4085:
            raise ValueError("image is not FAT12")
        if self.fat_bits == 16 and not 4085 <= self.cluster_count < 65525:
            raise ValueError("image is not FAT16")
        self.eoc = (1 << self.fat_bits) - 8

    @property
    def last_cluster(self) -> int:
        return self.cluster_count + 1

    def _fat_get(self, cluster: int) -> int:
        if self.fat_bits == 16:
            return struct.unpack_from("<H", self.image, self.fat_offset + cluster * 2)[0]
        offset = self.fat_offset + cluster + cluster // 2
        word = self.image[offset] | (self.image[offset + 1] << 8)
        return (word >> 4) & 0xFFF if cluster & 1 else word & 0xFFF

    def _fat_set(self, cluster: int, value: int) -> None:
        if not 0 <= value < (1 << self.fat_bits):
            raise ValueError("FAT value out of range")
        for copy_index in range(self.fat_count):
            base = self.fat_offset + copy_index * self.fat_bytes
            if self.fat_bits == 16:
                struct.pack_into("<H", self.image, base + cluster * 2, value)
                continue
            offset = base + cluster + cluster // 2
            word = self.image[offset] | (self.image[offset + 1] << 8)
            word = (word & 0x000F) | (value << 4) if cluster & 1 else (word & 0xF000) | value
            self.image[offset] = word & 0xFF
            self.image[offset + 1] = word >> 8

    def _cluster_offset(self, cluster: int) -> int:
        if not 2 <= cluster <= self.last_cluster:
            raise ValueError("cluster outside data area")
        return self.data_offset + (cluster - 2) * self.cluster_bytes

    def _chain(self, cluster: int) -> list[int]:
        result: list[int] = []
        seen: set[int] = set()
        while 2 <= cluster < self.eoc:
            if cluster in seen or cluster > self.last_cluster:
                raise ValueError("invalid FAT chain")
            seen.add(cluster)
            result.append(cluster)
            cluster = self._fat_get(cluster)
        if result and cluster < self.eoc:
            raise ValueError("unterminated FAT chain")
        return result

    def _directory_offsets(self, directory: str = "") -> list[int]:
        if not directory:
            return list(range(self.root_offset, self.root_offset + self.root_bytes, 32))
        entry = self._path_entry(directory)
        if not self.image[entry.offset + 11] & 0x10:
            raise ValueError(f"not a directory: {directory}")
        return [offset for cluster in self._chain(entry.first_cluster)
                for offset in range(self._cluster_offset(cluster),
                                    self._cluster_offset(cluster) + self.cluster_bytes, 32)]

    def entries(self, directory: str = "") -> list[DirectoryEntry]:
        result: list[DirectoryEntry] = []
        for offset in self._directory_offsets(directory):
            first = self.image[offset]
            if first == 0:
                continue
            if first == 0xE5 or self.image[offset + 11] == 0x0F:
                continue
            raw = bytes(self.image[offset:offset + 11])
            stem = raw[:8].decode("ascii", "replace").rstrip()
            suffix = raw[8:].decode("ascii", "replace").rstrip()
            result.append(
                DirectoryEntry(
                    stem if not suffix else f"{stem}.{suffix}",
                    offset,
                    struct.unpack_from("<H", self.image, offset + 26)[0],
                    struct.unpack_from("<I", self.image, offset + 28)[0],
                )
            )
        return result

    def _find(self, encoded_name: bytes, directory: str = "") -> DirectoryEntry | None:
        for entry in self.entries(directory):
            if self.image[entry.offset:entry.offset + 11] == encoded_name:
                return entry
        return None

    def _path_entry(self, filename: str) -> DirectoryEntry:
        parts = filename.replace("\\", "/").split("/")
        directory = ""
        for part in parts:
            entry = self._find(name83(part), directory)
            if entry is None:
                raise ValueError(f"missing expected file: {filename}")
            directory = directory + "/" + part if directory else part
        return entry

    def walk(self, directory: str = ""):
        for entry in self.entries(directory):
            attributes = self.image[entry.offset + 11]
            if entry.name in (".", "..") or attributes & 8:
                continue
            name = directory + "/" + entry.name if directory else entry.name
            if attributes & 0x10:
                yield name, None
                yield from self.walk(name)
            else:
                yield name, self.read(name)

    def _free_chain(self, cluster: int) -> None:
        seen: set[int] = set()
        while cluster >= 2 and cluster < self.eoc:
            if cluster in seen or cluster > self.last_cluster:
                raise ValueError("invalid FAT chain")
            seen.add(cluster)
            next_cluster = self._fat_get(cluster)
            self._fat_set(cluster, 0)
            cluster = next_cluster

    def _find_root_slot(self, directory: str = "") -> int:
        for offset in self._directory_offsets(directory):
            if self.image[offset] in (0, 0xE5):
                return offset
        if not directory:
            raise ValueError("root directory is full")
        entry = self._path_entry(directory)
        last = self._chain(entry.first_cluster)[-1]
        cluster = self._allocate(1)[0]
        self._fat_set(last, cluster)
        offset = self._cluster_offset(cluster)
        self.image[offset:offset + self.cluster_bytes] = bytes(self.cluster_bytes)
        return offset

    def _allocate(self, count: int) -> list[int]:
        free = [cluster for cluster in range(2, self.last_cluster + 1) if self._fat_get(cluster) == 0]
        if len(free) < count:
            raise ValueError(f"image needs {count} clusters but has {len(free)} free")
        selected = free[:count]
        for index, cluster in enumerate(selected):
            self._fat_set(cluster, selected[index + 1] if index + 1 < len(selected) else (1 << self.fat_bits) - 1)
        return selected

    def mkdir(self, directory: str) -> None:
        parent, _, name = directory.replace("\\", "/").rpartition("/")
        encoded = name83(name)
        if self._find(encoded, parent):
            raise ValueError(f"directory already exists: {directory}")
        slot = self._find_root_slot(parent)
        cluster = self._allocate(1)[0]
        self.image[slot:slot + 32] = bytes(32)
        self.image[slot:slot + 11] = encoded
        self.image[slot + 11] = 0x10
        struct.pack_into("<H", self.image, slot + 24, 0x0021)  # 1980-01-01
        struct.pack_into("<H", self.image, slot + 26, cluster)
        start = self._cluster_offset(cluster)
        self.image[start:start + self.cluster_bytes] = bytes(self.cluster_bytes)
        for index, (label, link) in enumerate(((b".          ", cluster),
                (b"..         ", self._path_entry(parent).first_cluster if parent else 0))):
            offset = start + index * 32
            self.image[offset:offset + 11] = label
            self.image[offset + 11] = 0x10
            struct.pack_into("<H", self.image, offset + 24, 0x0021)
            struct.pack_into("<H", self.image, offset + 26, link)

    def put(self, filename: str, data: bytes) -> None:
        directory, _, name = filename.replace("\\", "/").rpartition("/")
        encoded = name83(name)
        old = self._find(encoded, directory)
        offset = old.offset if old else self._find_root_slot(directory)
        if old and self.image[old.offset + 11] & 0x10:
            raise ValueError(f"cannot replace directory: {filename}")
        if old and old.first_cluster:
            self._free_chain(old.first_cluster)
        clusters = self._allocate((len(data) + self.cluster_bytes - 1) // self.cluster_bytes)
        for index, cluster in enumerate(clusters):
            start = self._cluster_offset(cluster)
            part = data[index * self.cluster_bytes:(index + 1) * self.cluster_bytes]
            self.image[start:start + self.cluster_bytes] = b"\0" * self.cluster_bytes
            self.image[start:start + len(part)] = part
        self.image[offset:offset + 32] = b"\0" * 32
        self.image[offset:offset + 11] = encoded
        self.image[offset + 11] = 0x20
        # DOS COPY propagates this date through the folder redirector. A zero
        # month/day is invalid and cannot be written through SdFat timestamp().
        struct.pack_into("<H", self.image, offset + 24, 0x0021)
        struct.pack_into("<H", self.image, offset + 26, clusters[0] if clusters else 0)
        struct.pack_into("<I", self.image, offset + 28, len(data))

    def read(self, filename: str) -> bytes:
        entry = self._path_entry(filename)
        remaining = entry.size
        cluster = entry.first_cluster
        output = bytearray()
        seen: set[int] = set()
        while remaining:
            if cluster < 2 or cluster >= self.eoc or cluster in seen:
                raise ValueError(f"invalid FAT chain for {filename}")
            seen.add(cluster)
            start = self._cluster_offset(cluster)
            copied = min(remaining, self.cluster_bytes)
            output += self.image[start:start + copied]
            remaining -= copied
            cluster = self._fat_get(cluster)
        return bytes(output)

    def free_clusters(self) -> int:
        return sum(self._fat_get(cluster) == 0 for cluster in range(2, self.last_cluster + 1))


class Fat16Root(Fat12Root):
    fat_bits = 16


def expanded_volume(source: Fat12Root) -> Fat16Root:
    boot_path = Path(__file__).resolve().parents[1] / "vendor/freedos-boot/boot16.bin"
    boot = boot_path.read_bytes()
    if sha256(boot) != FAT16_BOOT_SHA256 or len(boot) != 512:
        raise ValueError("unpinned FreeDOS FAT16 boot sector")
    # Keep the partition within complete cylinders advertised by the BIOS;
    # the exact 20 MiB container has ten harmless padding sectors at its end.
    sectors = (HARD_DISK_BYTES // 512 // HARD_DISK_SECTORS_PER_TRACK *
               HARD_DISK_SECTORS_PER_TRACK) - HARD_DISK_START_SECTOR
    image = bytearray(sectors * 512)
    image[:512] = boot
    image[3:62] = source.image[3:62]
    image[13] = 2  # 1 KiB clusters, a conventional FAT16 layout.
    struct.pack_into("<H", image, 17, 512)
    struct.pack_into("<H", image, 19, sectors)
    image[21] = 0xF8
    struct.pack_into("<H", image, 22, 80)
    struct.pack_into("<I", image, 32, 0)
    image[43:54] = b"MHS-DOSVM  "
    image[54:62] = b"FAT16   "
    volume = Fat16Root(image)
    if (volume.cluster_count + 2) * 2 > volume.fat_bytes:
        raise ValueError("FAT16 allocation table is too small")
    volume._fat_set(0, 0xFFF8)
    volume._fat_set(1, 0xFFFF)
    for name, contents in source.walk():
        if contents is None:
            volume.mkdir(name)
        else:
            volume.put(name, contents)
    return volume


def hard_disk_image(volume: Fat12Root) -> bytes:
    """Wrap the bootable volume in the fixed disk geometry MPE5 uses.

    8086tiny treats DL=80 as a hard disk with one head and 63 sectors per
    track.  A raw 1.44 MiB floppy passed through that path reads sector zero
    but calculates all later FreeDOS CHS reads with the wrong geometry.  The
    partitioned image lets the existing FreeDOS boot sector use its normal
    hidden-sector and BPB arithmetic.
    """
    volume_sectors = len(volume.image) // 512
    if len(volume.image) % 512 or not volume_sectors:
        raise ValueError("DOS volume is not sector-aligned")
    if volume_sectors > 0xFFFFFFFF:
        raise ValueError("DOS volume is too large for an MBR partition")

    struct.pack_into("<H", volume.image, 24, HARD_DISK_SECTORS_PER_TRACK)
    struct.pack_into("<H", volume.image, 26, HARD_DISK_HEADS)
    struct.pack_into("<I", volume.image, 28, HARD_DISK_START_SECTOR)
    volume.image[36] = 0x80  # BPB drive number: C:, delivered by BIOS as DL=80.

    # The BIOS derives its fixed-disk CHS geometry from the complete image
    # length. Pad through the last 63-sector track so its advertised final
    # cylinder includes the partition's final partial track.
    disk_sectors = HARD_DISK_BYTES // 512
    if HARD_DISK_START_SECTOR + volume_sectors > disk_sectors:
        raise ValueError("DOS volume exceeds its 20 MiB container")
    disk = bytearray(disk_sectors * 512)
    # Relocate the complete MBR to 0000:0600 before reading the PBR into
    # 0000:7c00. Reading over the executing MBR resumes inside the new BPB!
    # The far jump targets the relocated read stub at offset 30 (061e).
    mbr = bytes((
        0xFA, 0xFC, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0,
        0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0xFB,
        0xBE, 0x00, 0x7C, 0xBF, 0x00, 0x06, 0xB9, 0x00, 0x01,
        0xF3, 0xA5, 0xEA, 0x1E, 0x06, 0x00, 0x00,
        0xBB, 0x00, 0x7C, 0xB8, 0x01, 0x02, 0xB9, 0x01, 0x01,
        0xB6, 0x00, 0x52, 0xCD, 0x13, 0x5A, 0x72, 0xFE,
        0xEA, 0x00, 0x7C, 0x00, 0x00,
    ))
    disk[:len(mbr)] = mbr
    # One active FAT16 partition: starts at C/H/S 1/0/1 and ends within the
    # same one-head, 63-sector geometry advertised by the native BIOS.
    last = HARD_DISK_START_SECTOR + volume_sectors - 1
    end_cylinder, end_sector_zero = divmod(last, HARD_DISK_SECTORS_PER_TRACK)
    if end_cylinder > 1023:
        raise ValueError("DOS volume exceeds CHS limit for the native BIOS")
    partition = 0x1BE
    record = bytearray(16)
    record[:8] = bytes((
        0x80, 0x00, 0x01, 0x01,  # active, start C/H/S 1/0/1
        0x04 if volume.fat_bits == 16 else 0x01,
        0x00, end_sector_zero + 1, end_cylinder & 0xFF,
    ))
    record[6] |= ((end_cylinder >> 8) & 3) << 6
    struct.pack_into("<I", record, 8, HARD_DISK_START_SECTOR)
    struct.pack_into("<I", record, 12, volume_sectors)
    disk[partition:partition + 16] = record
    disk[510:512] = b"\x55\xAA"
    volume_offset = HARD_DISK_START_SECTOR * 512
    disk[volume_offset:volume_offset + len(volume.image)] = volume.image
    return bytes(disk)


def read_boot_image(source_zip: Path) -> bytearray:
    if file_sha256(source_zip).lower() != FREEDOS_SHA256:
        raise ValueError(f"{source_zip}: does not match the pinned FreeDOS source ZIP")
    with zipfile.ZipFile(source_zip) as archive:
        return bytearray(archive.read(BOOT_IMAGE_ENTRY))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-zip", required=True, type=Path, help="verified FreeDOS Floppy Edition ZIP")
    parser.add_argument("--command", required=True, type=Path,
                        help="pinned FreeCOM COMMAND.COM for the boot volume")
    parser.add_argument("--kssf", required=True, type=Path,
                        help="pinned FreeCOM 8086 conventional-memory swap helper")
    parser.add_argument("--boulder", required=True, type=Path, help="Boulder Dash DOS executable")
    parser.add_argument("--redirector", type=Path, help="DOSDIR.COM folder-sharing driver")
    parser.add_argument("--edit", required=True, type=Path, help="pinned FreeDOS EDIT.EXE")
    parser.add_argument("--edit-help", required=True, type=Path, help="pinned FreeDOS EDIT.HLP")
    parser.add_argument("--output", required=True, type=Path, help="output 20 MiB image")
    parser.add_argument("--manifest", type=Path, help="optional JSON validation record")
    parser.add_argument("--upgrade-dir", type=Path,
                        help="write non-destructive startup update files for SD DOSVM/D/DOSVMUPD")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_zip = args.source_zip.resolve()
    command = args.command.resolve()
    kssf = args.kssf.resolve()
    boulder = args.boulder.resolve()
    if not source_zip.is_file():
        raise ValueError(f"missing FreeDOS source ZIP: {source_zip}")
    if not boulder.is_file():
        raise ValueError(f"missing Boulder executable: {boulder}")
    if not command.is_file() or file_sha256(command).lower() != FREECOM_COMMAND_SHA256:
        raise ValueError("missing or unpinned FreeCOM KSWAP COMMAND.COM")
    if not kssf.is_file() or file_sha256(kssf).lower() != FREECOM_KSSF_SHA256:
        raise ValueError("missing or unpinned FreeCOM KSSF.COM")
    if not args.edit.is_file() or file_sha256(args.edit).lower() != FREEDOS_EDIT_SHA256:
        raise ValueError("missing or unpinned FreeDOS EDIT.EXE")
    if not args.edit_help.is_file() or file_sha256(args.edit_help).lower() != FREEDOS_EDIT_HELP_SHA256:
        raise ValueError("missing or unpinned FreeDOS EDIT.HLP")
    edit = args.edit.read_bytes()
    edit_help = args.edit_help.read_bytes()

    image = expanded_volume(Fat12Root(read_boot_image(source_zip)))
    payloads = {
        "AUTOEXEC.BAT": startup_autoexec(bool(args.redirector)),
        "CONFIG.SYS": CONFIG_SYS,
        # The FreeDOS Floppy Edition starts FDCONFIG.SYS before CONFIG.SYS.
        # Replace its language menu so the native VM reaches COMMAND.COM
        # without needing an invisible first keyboard selection.
        "FDCONFIG.SYS": CONFIG_SYS,
        "COMMAND.COM": command.read_bytes(),
        "KSSF.COM": kssf.read_bytes(),
        "CGA80.COM": CGA80_COM,
        "CGA40.COM": CGA40_COM,
        "PCTONE.COM": PCTONE_COM,
        "README.TXT": README_TXT,
        "BOULDER.EXE": boulder.read_bytes(),
        "FREEDOS/BIN/EDIT.EXE": edit,
        "FREEDOS/BIN/EDIT.HLP": edit_help,
    }
    if args.redirector:
        payloads["DOSDIR.COM"] = args.redirector.read_bytes()
    for name, data in payloads.items():
        image.put(name, data)
    expected = {name: {"bytes": len(data), "sha256": sha256(data)}
                for name, data in image.walk() if data is not None}
    for name, record in expected.items():
        actual = image.read(name)
        if len(actual) != record["bytes"] or sha256(actual) != record["sha256"]:
            raise ValueError(f"FAT16 validation failed for {name}")

    disk = hard_disk_image(image)
    if disk[510:512] != b"\x55\xAA" or disk[HARD_DISK_START_SECTOR * 512:
                                      HARD_DISK_START_SECTOR * 512 + 3] != b"\xEB\x3C\x90":
        raise ValueError("native hard-disk wrapper did not preserve the FreeDOS PBR")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
        temporary.write(disk)
        temporary_path = Path(temporary.name)
    temporary_path.replace(output)
    record = {
        "format": "mhs-dos-image-v3-fat16",
        "filesystem": "FAT16",
        "readOnly": False,
        "bootSectorSha256": FAT16_BOOT_SHA256,
        "freeDosZip": {"path": str(source_zip), "sha256": file_sha256(source_zip)},
        "freeCom": {
            "command": {"path": str(command), "sha256": file_sha256(command)},
            "kssf": {"path": str(kssf), "sha256": file_sha256(kssf)},
        },
        "boulder": {"path": str(boulder), "sha256": file_sha256(boulder), "bytes": boulder.stat().st_size},
        "image": {"path": str(output), "sha256": file_sha256(output), "bytes": output.stat().st_size},
        "files": expected,
        "freeClusters": image.free_clusters(),
        "clusterBytes": image.cluster_bytes,
        "freeBytes": image.free_clusters() * image.cluster_bytes,
        "partition": {
            "startSector": HARD_DISK_START_SECTOR,
            "sectors": len(image.image) // 512,
            "sectorsPerTrack": HARD_DISK_SECTORS_PER_TRACK,
            "heads": HARD_DISK_HEADS,
            "drive": "0x80",
            "diskSectors": len(disk) // 512,
        },
    }
    if args.upgrade_dir:
        record["startupUpgrade"] = write_startup_upgrade(
            args.upgrade_dir.resolve(), bool(args.redirector), edit, edit_help)
    if args.manifest:
        manifest = args.manifest.resolve()
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
