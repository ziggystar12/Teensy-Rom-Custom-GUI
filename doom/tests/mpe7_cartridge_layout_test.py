#!/usr/bin/env python3
"""Validate the exact DOOMVM EasyFlash/native-launcher layout."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--crt", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    data = args.crt.read_bytes()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    assert len(data) == 0x40 + 3 * (0x10 + 0x2000)
    assert data[:16] == b"C64 CARTRIDGE   "
    assert struct.unpack_from(">IHH", data, 16) == (0x40, 0x0100, 0x0020)
    assert data[32:64].rstrip(b"\0") == b"MHS DOOMVM"

    pages: dict[tuple[int, int], bytes] = {}
    offset = 0x40
    while offset < len(data):
        assert data[offset : offset + 4] == b"CHIP"
        length, chip_type, bank, address, size = struct.unpack_from(">IHHHH", data, offset + 4)
        assert (length, chip_type, size) == (0x2010, 2, 0x2000)
        assert bank != 58
        pages[(bank, address)] = data[offset + 0x10 : offset + length]
        offset += length
    assert set(pages) == {(0, 0x8000), (0, 0xA000), (1, 0x8000)}

    descriptor = pages[(1, 0x8000)]
    assert descriptor[:4] == b"M7D1"
    assert descriptor[4:8] == bytes((1, 16, 8, 64))
    path_bytes = struct.unpack_from("<I", descriptor, 8)[0]
    path_crc = struct.unpack_from("<I", descriptor, 12)[0]
    wad_path = descriptor[16 : 16 + path_bytes]
    assert wad_path == manifest["wadPath"].encode("ascii")
    assert zlib.crc32(wad_path) & 0xFFFFFFFF == path_crc
    assert manifest["format"] == "M7D1"
    assert manifest["cartridgeBytes"] == len(data)
    assert manifest["cartridgeSha256"] == hashlib.sha256(data).hexdigest()
    assert manifest["requiredPsramBytes"] == 8 * 1024 * 1024
    assert manifest["exclusiveRam2Bytes"] == 512 * 1024
    print(
        f"DOOMVM cartridge layout PASS: {len(data)} bytes, "
        f"M7D1 -> {wad_path.decode('ascii')}, RAM2=512 KiB, PSRAM=8 MiB"
    )


if __name__ == "__main__":
    main()
