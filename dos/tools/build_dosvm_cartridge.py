#!/usr/bin/env python3
"""Build the generic native DOSVM CRT selected from the TeensyROM+ GUI."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

PAGE_BYTES = 0x2000
HEADER_BYTES = 0x40
CHIP_HEADER_BYTES = 0x10
MAX_BIOS_BYTES = 0xFF00
NATIVE_LAUNCHER_TITLE = b"MHS DOSVM"
MPE3_MAILBOX_BANK = 58


def read_chips(data: bytes) -> list[tuple[int, int, bytes, bytes]]:
    if len(data) < HEADER_BYTES or data[:16] != b"C64 CARTRIDGE   ":
        raise ValueError("template is not a C64 CRT")
    offset = HEADER_BYTES
    chips: list[tuple[int, int, bytes, bytes]] = []
    while offset < len(data):
        header = data[offset : offset + CHIP_HEADER_BYTES]
        if len(header) != CHIP_HEADER_BYTES or header[:4] != b"CHIP":
            raise ValueError(f"invalid CHIP header at 0x{offset:x}")
        length, chip_type, bank, address, size = struct.unpack(">IHHHH", header[4:])
        if length != CHIP_HEADER_BYTES + size or size != PAGE_BYTES:
            raise ValueError(f"unsupported CHIP at 0x{offset:x}")
        payload = data[offset + CHIP_HEADER_BYTES : offset + length]
        chips.append((bank, address, header, payload))
        offset += length
    if offset != len(data):
        raise ValueError("trailing CRT data")
    return chips


def chip(bank: int, address: int, payload: bytes) -> bytes:
    if len(payload) != PAGE_BYTES:
        raise ValueError("native CRT pages must be eight KiB")
    return b"CHIP" + struct.pack(">IHHHH", CHIP_HEADER_BYTES + PAGE_BYTES,
                                  2, bank, address, PAGE_BYTES) + payload


def cartridge_header() -> bytes:
    """Create the M3 launcher header recognized by the native directory."""
    header = bytearray(HEADER_BYTES)
    header[:16] = b"C64 CARTRIDGE   "
    struct.pack_into(">IHH", header, 16, HEADER_BYTES, 0x0100, 0x0020)
    header[24] = 1  # EasyFlash EXROM asserted, GAME released.
    header[32:32 + len(NATIVE_LAUNCHER_TITLE)] = NATIVE_LAUNCHER_TITLE
    return bytes(header)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot-bank", type=Path,
                        default=Path("build/dos-work/dosvm-bootbank.bin"))
    parser.add_argument("--bios", type=Path,
                        default=Path("engine/native-dos/vendor/8086tiny/bios"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    boot_bank = args.boot_bank.read_bytes()
    if len(boot_bank) != PAGE_BYTES * 2:
        raise ValueError("DOSVM boot bank must be exactly 16 KiB")

    bios = args.bios.read_bytes()
    if not bios or len(bios) > MAX_BIOS_BYTES:
        raise ValueError(f"8086tiny BIOS must be 1 to {MAX_BIOS_BYTES} bytes")
    native_header = bytearray(16)
    native_header[:4] = b"M5D1"
    native_header[4] = 1
    native_header[5] = len(native_header)
    native_header[8:12] = struct.pack("<I", len(bios))
    native_header[12:16] = struct.pack("<I", zlib_crc32(bios))
    raw_length = PAGE_BYTES * 2 + len(native_header) + len(bios)
    raw = bytearray((raw_length + PAGE_BYTES - 1) // PAGE_BYTES * PAGE_BYTES)
    raw[PAGE_BYTES * 2 : PAGE_BYTES * 2 + len(native_header)] = native_header
    raw[PAGE_BYTES * 2 + len(native_header) : PAGE_BYTES * 2 + len(native_header) + len(bios)] = bios
    raw_bytes = bytes(raw)

    out = bytearray(cartridge_header())
    out.extend(chip(0, 0x8000, boot_bank[:PAGE_BYTES]))
    out.extend(chip(0, 0xA000, boot_bank[PAGE_BYTES:]))
    for page in range(2, len(raw_bytes) // PAGE_BYTES):
        offset = page * PAGE_BYTES
        out.extend(chip(page // 2, 0x8000 if page % 2 == 0 else 0xA000,
                        raw_bytes[offset : offset + PAGE_BYTES]))
    # Bank 58 is the reserved IO2 mailbox and must have no CHIP records.
    # The shared C64 launcher executes in RAM with cartridge ROM mapped out
    # before selecting it, matching the native AGI cartridge directory ABI.

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out)
    parsed = read_chips(bytes(out))
    pages = {(bank * 2) + (address == 0xA000): payload
             for bank, address, _, payload in parsed}
    rebuilt = b"".join(pages[index] for index in range(2, len(raw_bytes) // PAGE_BYTES))
    if rebuilt != raw_bytes[PAGE_BYTES * 2 :] or rebuilt[:4] != b"M5D1":
        raise ValueError("generated CRT did not preserve the M5D1 payload")
    if any(bank == MPE3_MAILBOX_BANK for bank, _, _, _ in parsed):
        raise ValueError("reserved M3 mailbox bank must not contain ROM pages")
    manifest = {
        "format": "M5D1",
        "protocol": 1,
        "cartridge": str(args.output),
        "cartridgeSha256": hashlib.sha256(out).hexdigest(),
        "cartridgeBytes": len(out),
        "bootPages": 2,
        "m3MailboxBank": MPE3_MAILBOX_BANK,
        "m3MailboxPages": 0,
        "nativePages": len(raw_bytes) // PAGE_BYTES - 2,
        "bootBankSha256": hashlib.sha256(boot_bank).hexdigest(),
        "bootBankBytes": len(boot_bank),
        "nativeLauncherId": NATIVE_LAUNCHER_TITLE.decode("ascii"),
        "biosSha256": hashlib.sha256(bios).hexdigest(),
        "biosBytes": len(bios),
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def zlib_crc32(data: bytes) -> int:
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


if __name__ == "__main__":
    main()
