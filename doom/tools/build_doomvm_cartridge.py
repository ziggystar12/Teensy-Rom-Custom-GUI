#!/usr/bin/env python3
"""Build the small native DOOMVM launcher cartridge."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

PAGE_BYTES = 0x2000
HEADER_BYTES = 0x40
CHIP_HEADER_BYTES = 0x10
MPE3_MAILBOX_BANK = 58
NATIVE_LAUNCHER_TITLE = b"MHS DOOMVM"
DEFAULT_WAD_PATH = "/DOOMVM/DOOM1.WAD"


def chip(bank: int, address: int, payload: bytes) -> bytes:
    if len(payload) != PAGE_BYTES:
        raise ValueError("native CRT pages must be eight KiB")
    return b"CHIP" + struct.pack(
        ">IHHHH", CHIP_HEADER_BYTES + PAGE_BYTES, 2, bank, address, PAGE_BYTES
    ) + payload


def cartridge_header() -> bytes:
    header = bytearray(HEADER_BYTES)
    header[:16] = b"C64 CARTRIDGE   "
    struct.pack_into(">IHH", header, 16, HEADER_BYTES, 0x0100, 0x0020)
    header[24] = 1
    header[32 : 32 + len(NATIVE_LAUNCHER_TITLE)] = NATIVE_LAUNCHER_TITLE
    return bytes(header)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot-bank", type=Path, required=True)
    parser.add_argument("--wad-path", default=DEFAULT_WAD_PATH)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    boot_bank = args.boot_bank.read_bytes()
    if len(boot_bank) != PAGE_BYTES * 2:
        raise ValueError("DOOMVM boot bank must be exactly 16 KiB")

    wad_path = args.wad_path.encode("ascii")
    if not wad_path.startswith(b"/") or len(wad_path) > 255 or b"\0" in wad_path:
        raise ValueError("WAD path must be an absolute ASCII SD path of at most 255 bytes")

    # M7D1 v1 descriptor. MPE6/N6D1 belongs to NESVM. The core lives in
    # firmware; the cartridge supplies
    # only its SD WAD path and explicit hardware memory contract.
    descriptor = bytearray(16)
    descriptor[:4] = b"M7D1"
    descriptor[4] = 1
    descriptor[5] = len(descriptor)
    descriptor[6] = 8  # required external PSRAM MiB for the MCUME zone
    descriptor[7] = 64  # exclusive RAM2 ownership in 8 KiB blocks (512 KiB)
    struct.pack_into("<I", descriptor, 8, len(wad_path))
    struct.pack_into("<I", descriptor, 12, zlib.crc32(wad_path) & 0xFFFFFFFF)

    native_page = bytearray(PAGE_BYTES)
    native_page[: len(descriptor)] = descriptor
    native_page[len(descriptor) : len(descriptor) + len(wad_path)] = wad_path

    output = bytearray(cartridge_header())
    output.extend(chip(0, 0x8000, boot_bank[:PAGE_BYTES]))
    output.extend(chip(0, 0xA000, boot_bank[PAGE_BYTES:]))
    output.extend(chip(1, 0x8000, bytes(native_page)))

    if any(bank == MPE3_MAILBOX_BANK for bank in (0, 1)):
        raise AssertionError("reserved MPE mailbox bank must not contain ROM pages")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    manifest = {
        "format": "M7D1",
        "protocol": 1,
        "cartridge": str(args.output),
        "cartridgeSha256": hashlib.sha256(output).hexdigest(),
        "cartridgeBytes": len(output),
        "bootPages": 2,
        "nativePages": 1,
        "m3MailboxBank": MPE3_MAILBOX_BANK,
        "m3MailboxPages": 0,
        "nativeLauncherId": NATIVE_LAUNCHER_TITLE.decode("ascii"),
        "wadPath": args.wad_path,
        "wadPathBytes": len(wad_path),
        "wadPathCrc32": f"{zlib.crc32(wad_path) & 0xFFFFFFFF:08x}",
        "requiredPsramBytes": 8 * 1024 * 1024,
        "exclusiveRam2Bytes": 512 * 1024,
        "bootBankSha256": hashlib.sha256(boot_bank).hexdigest(),
        "bootBankBytes": len(boot_bank),
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
