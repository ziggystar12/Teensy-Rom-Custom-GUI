#!/usr/bin/env python3
"""Create the exact SD-card subtree consumed by the DOSVM native session."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil


def copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise ValueError(f"required input is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cartridge", type=Path, required=True)
    parser.add_argument("--cartridge-manifest", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--image-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    copy(args.cartridge, args.output / "DOSVM.CRT")
    copy(args.cartridge_manifest, args.output / "DOSVM" / "DOSVM.CRT.JSON")
    copy(args.image, args.output / "DOSVM" / "DOSVM.IMG")
    copy(args.image_manifest, args.output / "DOSVM" / "DOSVM.JSON")
    shared = args.output / "DOSVM" / "D"
    shared.mkdir(parents=True, exist_ok=True)
    (shared / "README.TXT").write_bytes(
        b"This SD folder is DOS drive D:.\r\n"
        b"Copy DOS games and files here using your PC, then launch DOSVM.CRT.\r\n"
        b"Use short 8.3 names, for example GAMES\\BOULDER.EXE.\r\n"
        b"DOS changes here are saved directly to the SD card.\r\n")


if __name__ == "__main__":
    main()
