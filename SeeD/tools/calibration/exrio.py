"""Minimal OpenEXR reader for the calibration harness.

Handles uncompressed (codec NONE) scanline EXRs, which is what both sides of
the comparison produce: Blender is told to write exr_codec='NONE', and SeeD's
ImageWriter only emits uncompressed scanlines. That keeps this dependency-free
-- no OpenEXR, and no zlib beyond what the stdlib already has.

Works with or without numpy. With numpy the channel data comes back as float32
arrays; without it, as array.array('f'). Both index the same way.
"""
import struct
from array import array

try:
    import numpy as _np
except ImportError:
    _np = None

MAGIC = 0x01312F76
UINT, HALF, FLOAT = 0, 1, 2
_PIXEL_SIZE = {UINT: 4, HALF: 2, FLOAT: 4}


class ExrError(Exception):
    pass


def _read_null_str(buf, pos):
    end = buf.index(b"\0", pos)
    return buf[pos:end].decode("utf-8", "replace"), end + 1


def _half_to_float_table():
    # 65536-entry lookup; far faster than decoding halves one at a time.
    tbl = array("f", [0.0]) * 65536
    for h in range(65536):
        sign = -1.0 if (h >> 15) else 1.0
        exp = (h >> 10) & 0x1F
        man = h & 0x3FF
        if exp == 0:
            val = man * 2.0 ** -24
        elif exp == 31:
            val = float("inf") if man == 0 else float("nan")
        else:
            val = (1.0 + man / 1024.0) * 2.0 ** (exp - 15)
        tbl[h] = sign * val
    return tbl


_HALF_TBL = None


def _halves_to_floats(raw, count):
    global _HALF_TBL
    if _np is not None:
        return _np.frombuffer(raw, dtype=_np.float16, count=count).astype(_np.float32)
    if _HALF_TBL is None:
        _HALF_TBL = _half_to_float_table()
    words = array("H")
    words.frombytes(raw[: count * 2])
    return array("f", [_HALF_TBL[w] for w in words])


def read_exr(path):
    """Return (width, height, {channel_name: float array}), row 0 at the top."""
    with open(path, "rb") as f:
        buf = f.read()

    magic, = struct.unpack_from("<I", buf, 0)
    if magic != MAGIC:
        raise ExrError("%s is not an EXR file" % path)
    version, = struct.unpack_from("<I", buf, 4)
    flags = version >> 8
    if flags & 0x200:
        raise ExrError("tiled EXR is not supported: %s" % path)
    if flags & 0x1800:
        raise ExrError("deep or multi-part EXR is not supported: %s" % path)

    pos = 8
    attrs = {}
    channels = []
    while True:
        if buf[pos] == 0:
            pos += 1
            break
        name, pos = _read_null_str(buf, pos)
        atype, pos = _read_null_str(buf, pos)
        size, = struct.unpack_from("<i", buf, pos)
        pos += 4
        data = buf[pos:pos + size]
        pos += size
        if name == "channels":
            cpos = 0
            while data[cpos] != 0:
                cname, cpos = _read_null_str(data, cpos)
                ptype, = struct.unpack_from("<i", data, cpos)
                cpos += 16  # pixelType + pLinear + 3 reserved + xSampling + ySampling
                channels.append((cname, ptype))
        else:
            attrs[name] = (atype, data)

    if "dataWindow" not in attrs:
        raise ExrError("EXR has no dataWindow: %s" % path)
    x_min, y_min, x_max, y_max = struct.unpack_from("<iiii", attrs["dataWindow"][1], 0)
    width = x_max - x_min + 1
    height = y_max - y_min + 1

    comp = attrs.get("compression", (None, b"\0"))[1][0]
    if comp != 0:
        raise ExrError(
            "%s uses EXR compression %d; the harness only reads uncompressed "
            "(write it with exr_codec='NONE')" % (path, comp))

    # Channels are stored alphabetically within each scanline.
    channels.sort(key=lambda c: c[0])
    row_bytes = sum(_PIXEL_SIZE[t] * width for _, t in channels)

    if _np is not None:
        out = {c: _np.empty(width * height, dtype=_np.float32) for c, _ in channels}
    else:
        out = {c: array("f", bytes(4 * width * height)) for c, _ in channels}

    offsets = struct.unpack_from("<%dQ" % height, buf, pos)
    for off in offsets:
        y, = struct.unpack_from("<i", buf, off)
        size, = struct.unpack_from("<i", buf, off + 4)
        if size != row_bytes:
            raise ExrError("unexpected scanline size in %s" % path)
        p = off + 8
        row = y - y_min
        for cname, ptype in channels:
            n = width
            if ptype == FLOAT:
                if _np is not None:
                    vals = _np.frombuffer(buf, dtype=_np.float32, count=n, offset=p)
                else:
                    vals = array("f")
                    vals.frombytes(buf[p:p + 4 * n])
            elif ptype == HALF:
                vals = _halves_to_floats(buf[p:p + 2 * n], n)
            else:
                raise ExrError("UINT channels are not supported: %s" % path)
            out[cname][row * width:(row + 1) * width] = vals
            p += _PIXEL_SIZE[ptype] * n

    return width, height, out


def rgb(path):
    """Convenience: (width, height, R, G, B) with missing channels as zeros."""
    w, h, ch = read_exr(path)
    zero = (_np.zeros(w * h, dtype=_np.float32) if _np is not None
            else array("f", bytes(4 * w * h)))
    return w, h, ch.get("R", zero), ch.get("G", zero), ch.get("B", zero)
