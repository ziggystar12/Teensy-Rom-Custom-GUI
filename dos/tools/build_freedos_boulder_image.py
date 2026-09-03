#!/usr/bin/env python3
"""Build the read-only FreeDOS/Boulder test floppy for the native x86 proof.

The builder extracts the official FreeDOS 1.44 MiB boot disk, retains its boot
sector and system files, and replaces only the configuration files plus the
small test payload.  It implements the small FAT12 subset required for root
directory files so it has no host dependency on mtools.
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
FREECOM_COMMAND_SHA256 = "ae6aee6b18360c5408e5293fe906ab9b333158a32b50d604ca32177711aab768"
FREECOM_KSSF_SHA256 = "ab26a437879069efb378636f96524fa90bc0f58d3150f0f456486963e5052a76"

# INT 10h, mode 1 (40x25 colour text), then DOS terminate with success.
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
    "CGA40\r\n"
    "CLS\r\n"
    "ECHO MHS POWER ENGINE - FreeDOS VM proof\r\n"
    "ECHO Type DIR or VER to test the prompt.\r\n"
    "ECHO PCTONE tests sound; BOULDER tests CGA.\r\n"
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
    "MHS Power Engine FreeDOS VM proof disk\r\n"
    "\r\n"
    "At the C:\\ prompt, type:\r\n"
    "  DIR\r\n"
    "  VER\r\n"
    "  PCTONE   - PC speaker tone, then return to DOS\r\n"
    "  BOULDER  - CGA graphics test\r\n"
    "\r\n"
    "This is a read-only test disk.\r\n"
    "PCTONE programs the PC PIT for an approximately 1 kHz tone,\r\n"
    "then switches the speaker off and returns to the prompt.\r\n"
    "BOULDER starts its CGA title screen. This edition defaults\r\n"
    "to joystick control; keyboard-only gameplay is under test.\r\n"
).encode("ascii")


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
        if self.cluster_count >= 4085:
            raise ValueError("image is not FAT12")

    @property
    def last_cluster(self) -> int:
        return self.cluster_count + 1

    def _fat_get(self, cluster: int) -> int:
        offset = self.fat_offset + cluster + cluster // 2
        word = self.image[offset] | (self.image[offset + 1] << 8)
        return (word >> 4) & 0xFFF if cluster & 1 else word & 0xFFF

    def _fat_set(self, cluster: int, value: int) -> None:
        if not 0 <= value <= 0xFFF:
            raise ValueError("FAT12 value out of range")
        for copy_index in range(self.fat_count):
            base = self.fat_offset + copy_index * self.fat_bytes
            offset = base + cluster + cluster // 2
            word = self.image[offset] | (self.image[offset + 1] << 8)
            word = (word & 0x000F) | (value << 4) if cluster & 1 else (word & 0xF000) | value
            self.image[offset] = word & 0xFF
            self.image[offset + 1] = word >> 8

    def _cluster_offset(self, cluster: int) -> int:
        if not 2 <= cluster <= self.last_cluster:
            raise ValueError("cluster outside data area")
        return self.data_offset + (cluster - 2) * self.cluster_bytes

    def entries(self) -> list[DirectoryEntry]:
        result: list[DirectoryEntry] = []
        for offset in range(self.root_offset, self.root_offset + self.root_bytes, 32):
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

    def _find(self, encoded_name: bytes) -> DirectoryEntry | None:
        for entry in self.entries():
            if self.image[entry.offset:entry.offset + 11] == encoded_name:
                return entry
        return None

    def _free_chain(self, cluster: int) -> None:
        seen: set[int] = set()
        while cluster >= 2 and cluster < 0xFF8:
            if cluster in seen or cluster > self.last_cluster:
                raise ValueError("invalid FAT chain")
            seen.add(cluster)
            next_cluster = self._fat_get(cluster)
            self._fat_set(cluster, 0)
            cluster = next_cluster

    def _find_root_slot(self) -> int:
        for offset in range(self.root_offset, self.root_offset + self.root_bytes, 32):
            if self.image[offset] in (0, 0xE5):
                return offset
        raise ValueError("root directory is full")

    def _allocate(self, count: int) -> list[int]:
        free = [cluster for cluster in range(2, self.last_cluster + 1) if self._fat_get(cluster) == 0]
        if len(free) < count:
            raise ValueError(f"image needs {count} clusters but has {len(free)} free")
        selected = free[:count]
        for index, cluster in enumerate(selected):
            self._fat_set(cluster, selected[index + 1] if index + 1 < len(selected) else FAT12_EOC)
        return selected

    def put(self, filename: str, data: bytes) -> None:
        if not data:
            raise ValueError(f"empty DOS file is not supported: {filename}")
        encoded = name83(filename)
        old = self._find(encoded)
        offset = old.offset if old else self._find_root_slot()
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
        struct.pack_into("<H", self.image, offset + 26, clusters[0])
        struct.pack_into("<I", self.image, offset + 28, len(data))

    def read(self, filename: str) -> bytes:
        entry = self._find(name83(filename))
        if entry is None:
            raise ValueError(f"missing expected file: {filename}")
        remaining = entry.size
        cluster = entry.first_cluster
        output = bytearray()
        seen: set[int] = set()
        while remaining:
            if cluster < 2 or cluster >= 0xFF8 or cluster in seen:
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


def hard_disk_image(volume: Fat12Root) -> bytes:
    """Wrap the bootable FAT12 volume in the fixed disk geometry MPE5 uses.

    8086tiny treats DL=80 as a hard disk with one head and 63 sectors per
    track.  A raw 1.44 MiB floppy passed through that path reads sector zero
    but calculates all later FreeDOS CHS reads with the wrong geometry.  The
    partitioned image lets the existing FreeDOS boot sector use its normal
    hidden-sector and BPB arithmetic while preserving the simple FAT12 root.
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
    disk_sectors = ((HARD_DISK_START_SECTOR + volume_sectors +
                     HARD_DISK_SECTORS_PER_TRACK - 1) // HARD_DISK_SECTORS_PER_TRACK *
                    HARD_DISK_SECTORS_PER_TRACK)
    disk = bytearray(disk_sectors * 512)
    # Minimal MBR: read active partition sector C/H/S 1/0/1 to 0000:7c00 and
    # transfer control with the BIOS-provided DL drive number unchanged.
    disk[:29] = bytes((
        0xFA, 0xFC, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0,
        0xBB, 0x00, 0x7C, 0xB8, 0x01, 0x02, 0xB5, 0x01,
        0xB1, 0x01, 0xB6, 0x00, 0xCD, 0x13, 0x72, 0xFE,
        0xEA, 0x00, 0x7C, 0x00, 0x00,
    ))
    # One active FAT12 partition: starts at C/H/S 1/0/1 and ends within the
    # same one-head, 63-sector geometry advertised by the native BIOS.
    last = HARD_DISK_START_SECTOR + volume_sectors - 1
    end_cylinder, end_sector_zero = divmod(last, HARD_DISK_SECTORS_PER_TRACK)
    if end_cylinder > 1023:
        raise ValueError("DOS volume exceeds CHS limit for the native BIOS")
    partition = 0x1BE
    record = bytearray(16)
    record[:8] = bytes((
        0x80, 0x00, 0x01, 0x01,  # active, start C/H/S 1/0/1
        0x01, 0x00, end_sector_zero + 1, end_cylinder & 0xFF,
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
    parser.add_argument("--output", required=True, type=Path, help="output 1.44 MiB image")
    parser.add_argument("--manifest", type=Path, help="optional JSON validation record")
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

    image = Fat12Root(read_boot_image(source_zip))
    payloads = {
        "AUTOEXEC.BAT": AUTOEXEC_BAT,
        "CONFIG.SYS": CONFIG_SYS,
        # The FreeDOS Floppy Edition starts FDCONFIG.SYS before CONFIG.SYS.
        # Replace its language menu so the native VM reaches COMMAND.COM
        # without needing an invisible first keyboard selection.
        "FDCONFIG.SYS": CONFIG_SYS,
        "COMMAND.COM": command.read_bytes(),
        "KSSF.COM": kssf.read_bytes(),
        "CGA40.COM": CGA40_COM,
        "PCTONE.COM": PCTONE_COM,
        "README.TXT": README_TXT,
        "BOULDER.EXE": boulder.read_bytes(),
    }
    for name, data in payloads.items():
        image.put(name, data)
    expected = {name: {"bytes": len(data), "sha256": sha256(data)} for name, data in payloads.items()}
    for name, record in expected.items():
        actual = image.read(name)
        if len(actual) != record["bytes"] or sha256(actual) != record["sha256"]:
            raise ValueError(f"FAT12 validation failed for {name}")

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
        "format": "mhs-dos-proof-image-v2-hard-disk",
        "freeDosZip": {"path": str(source_zip), "sha256": file_sha256(source_zip)},
        "freeCom": {
            "command": {"path": str(command), "sha256": file_sha256(command)},
            "kssf": {"path": str(kssf), "sha256": file_sha256(kssf)},
        },
        "boulder": {"path": str(boulder), "sha256": file_sha256(boulder), "bytes": boulder.stat().st_size},
        "image": {"path": str(output), "sha256": file_sha256(output), "bytes": output.stat().st_size},
        "files": expected,
        "freeClusters": image.free_clusters(),
        "partition": {
            "startSector": HARD_DISK_START_SECTOR,
            "sectors": len(image.image) // 512,
            "sectorsPerTrack": HARD_DISK_SECTORS_PER_TRACK,
            "heads": HARD_DISK_HEADS,
            "drive": "0x80",
            "diskSectors": len(disk) // 512,
        },
    }
    if args.manifest:
        manifest = args.manifest.resolve()
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
