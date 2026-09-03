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
    # Preallocate the scratch backing file. Firmware invalidates its page
    # bitmap on every launch, so these bytes are never persistent guest RAM.
    virtual_bytes = 0x10FFF0 + 65536 + (80 + 40) * 25 * 2
    swap_bytes = ((virtual_bytes + 511) // 512) * 512
    (args.output / "DOSVM" / "DOSVM.SWP").write_bytes(bytes(swap_bytes))


if __name__ == "__main__":
    main()
