"""Tiny PNG writer plus the linear-to-display conversion the previews use.

Only stdlib (zlib, struct), so the harness needs nothing installed to emit
its comparison images.
"""
import struct
import zlib


def write_rgb(path, width, height, pixels):
    """Write 8-bit RGB. `pixels` is a bytes-like of length width*height*3."""
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)                                  # filter type 0 (None)
        raw += bytes(pixels[y * stride:(y + 1) * stride])

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def _srgb(x):
    if x <= 0.0:
        return 0
    if x <= 0.0031308:
        v = x * 12.92
    else:
        v = 1.055 * (x ** (1.0 / 2.4)) - 0.055
    return 255 if v >= 1.0 else int(v * 255.0 + 0.5)


_SRGB_LUT = [_srgb(i / 4095.0) for i in range(4096)]


def encode_srgb(value):
    """Linear scene value -> 8-bit sRGB, clamped."""
    if value <= 0.0:
        return 0
    if value >= 1.0:
        return 255
    return _SRGB_LUT[int(value * 4095.0)]


def tonemap(width, height, r, g, b, exposure=1.0):
    """Flat exposure then an sRGB encode -- no filmic curve, so the preview
    stays a readable proxy for the linear numbers rather than a look."""
    out = bytearray(width * height * 3)
    e = exposure
    for i in range(width * height):
        j = i * 3
        out[j] = encode_srgb(r[i] * e)
        out[j + 1] = encode_srgb(g[i] * e)
        out[j + 2] = encode_srgb(b[i] * e)
    return out
