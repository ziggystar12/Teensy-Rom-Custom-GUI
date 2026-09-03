#!/usr/bin/env python3
"""Fetch the pinned FreeCOM 0.86 8086 KSWAP command processor."""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import tempfile
import urllib.request
import zipfile

URL = "https://github.com/FDOS/freecom/releases/download/com086/English.zip"
ARCHIVE_SHA256 = "42093a00286f66bf922eebd8204a3b74b429f4c6973cd960ae1ab52ce80b4bd5"
FILES = {
    "kswap/command.com": ("COMMAND.COM", "ae6aee6b18360c5408e5293fe906ab9b333158a32b50d604ca32177711aab768"),
    "kswap/kssf.com": ("KSSF.COM", "ab26a437879069efb378636f96524fa90bc0f58d3150f0f456486963e5052a76"),
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
        raise ValueError("FreeCOM archive does not match the pinned SHA-256")
    with zipfile.ZipFile(io.BytesIO(archive)) as package:
        for member, (name, expected) in FILES.items():
            data = package.read(member)
            if sha256(data) != expected:
                raise ValueError(f"FreeCOM payload hash mismatch: {member}")
            write(args.output / name, data)
    print(f"Fetched pinned FreeCOM 0.86 KSWAP files to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
