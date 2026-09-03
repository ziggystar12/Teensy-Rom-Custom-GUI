"""Render bitmap planes captured from the executed C64 terminal."""
import argparse
import json
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parents[2]
work = root / "build" / "dos-work"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--planes', type=Path, default=work / 'dos-c64-planes.bin')
parser.add_argument('--frame', type=Path, help='JSON with hires and background fields')
parser.add_argument('--output', type=Path, default=work / 'dos-screen.png')
args = parser.parse_args()
frame = json.loads(args.frame.read_text()) if args.frame else {'hires': True, 'background': 0}
planes = args.planes.read_bytes()
if len(planes) != 10000:
    raise ValueError("Expected 8000 bitmap, 1000 screen, 1000 colour bytes")
palette = (
    (0, 0, 0), (255, 255, 255), (136, 57, 50), (103, 182, 189),
    (139, 63, 150), (85, 160, 73), (64, 49, 141), (191, 206, 114),
    (139, 84, 41), (87, 66, 0), (184, 105, 98), (80, 80, 80),
    (120, 120, 120), (148, 224, 137), (120, 105, 196), (159, 159, 159),
)
width, height = 640, 400
pixels = bytearray(width * height * 3)
for cell in range(1000):
    attribute = planes[8000 + cell]
    for row in range(8):
        bits = planes[cell * 8 + row]
        for column in range(8):
            if frame['hires']:
                colour = attribute >> 4 if bits & (128 >> column) else attribute & 15
            else:
                colours = (frame['background'], attribute >> 4, attribute & 15, planes[9000 + cell] & 15)
                colour = colours[(bits >> (6 - (column // 2) * 2)) & 3]
            x, y = (cell % 40 * 8 + column) * 2, (cell // 40 * 8 + row) * 2
            for dy in (0, 1):
                offset = ((y + dy) * width + x) * 3
                pixels[offset:offset + 6] = bytes(palette[colour]) * 2

# Standard PNG chunks keep this deterministic preview dependency-free. Each
# native pixel becomes exactly four pixels, with no smoothing or font rerender.
def chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3]
                for y in range(height))
png = (b"\x89PNG\r\n\x1a\n" +
       chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
args.output.write_bytes(png)
print(args.output)
