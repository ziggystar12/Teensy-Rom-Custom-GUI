#!/usr/bin/env python3
"""Fetch the pinned official FreeDOS Edit 0.9c executable and help file."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import tempfile
import urllib.request
import zipfile

URL = "https://ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/latest/base/edit/20250530.1/edit.zip"
ARCHIVE_SHA256 = "244edc7f1aa4cd3680d9341dc67cac268df7a3e4910ab53a71272fb1925cf31f"
FILES = {
    "BIN/edit.exe": ("EDIT.EXE", "e972ca9f5b25e97e2959057809a1f640123649c3da76971ec829ced6cbbe1ced"),
    "BIN/edit.hlp": ("EDIT.HLP", "9c90eac60b8065d1d12f13af679b7895512eb76d3007e107e755f68f5b9d2265"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
        temporary.write(data)
        temporary_path = Path(temporary.name)
    temporary_path.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    with urllib.request.urlopen(URL) as response:
        archive = response.read()
    if sha256(archive) != ARCHIVE_SHA256:
        raise ValueError("FreeDOS Edit archive does not match the pinned SHA-256")
    with zipfile.ZipFile(io.BytesIO(archive)) as package:
        for member, (name, expected) in FILES.items():
            data = package.read(member)
            if sha256(data) != expected:
                raise ValueError(f"FreeDOS Edit payload hash mismatch: {member}")
            write(args.output / name, data)
    print(f"Fetched pinned FreeDOS Edit 0.9c files to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
