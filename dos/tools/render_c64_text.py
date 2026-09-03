"""Render the final hires planes captured from the executed C64 terminal."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[2]
work = root / "build" / "dos-work"
planes = (work / "dos-c64-planes.bin").read_bytes()
if len(planes) != 10000:
    raise ValueError("Expected 8000 bitmap, 1000 screen, 1000 colour bytes")
palette = (
    (0, 0, 0), (255, 255, 255), (136, 57, 50), (103, 182, 189),
    (139, 63, 150), (85, 160, 73), (64, 49, 141), (191, 206, 114),
    (139, 84, 41), (87, 66, 0), (184, 105, 98), (80, 80, 80),
    (120, 120, 120), (148, 224, 137), (120, 105, 196), (159, 159, 159),
)
image = Image.new("RGB", (320, 200))
pixels = image.load()
for cell in range(1000):
    attribute = planes[8000 + cell]
    for row in range(8):
        bits = planes[cell * 8 + row]
        for column in range(8):
            colour = attribute >> 4 if bits & (128 >> column) else attribute & 15
            pixels[cell % 40 * 8 + column, cell // 40 * 8 + row] = palette[colour]
image.resize((640, 400), Image.Resampling.NEAREST).save(work / "dos-screen.png")
print(work / "dos-screen.png")
