"""Convert this application's own GL framebuffer capture to PNG (no screen capture)."""
from pathlib import Path
import struct
import sys
import zlib

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/desktop-capture.rgba")
destination = source.with_suffix(".png")
width, height = 1280, 720
rgba = source.read_bytes()
if len(rgba) != width * height * 4:
    raise ValueError("Expected native 1280x720 RGBA capture")
stride = width * 4
pixels = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height - 1, -1, -1))

def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

destination.write_bytes(b"\x89PNG\r\n\x1a\n" +
    chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
    chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
print(destination)
