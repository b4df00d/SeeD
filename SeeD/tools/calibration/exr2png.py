"""Eyeball an HDR image from the harness.

    python exr2png.py in.exr out.png [exposure]

Flat exposure plus an sRGB encode -- deliberately not a filmic curve, so what
you see stays a readable proxy for the linear numbers being compared.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exrio
import pngio


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    exposure = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0

    w, h, r, g, b = exrio.rgb(src)
    pngio.write_rgb(dst, w, h, pngio.tonemap(w, h, r, g, b, exposure))

    n = w * h
    lum = [0.2126 * r[i] + 0.7152 * g[i] + 0.0722 * b[i] for i in range(n)]
    lum.sort()
    print("%s -> %s  %dx%d  exposure %.3f" % (src, dst, w, h, exposure))
    print("  luminance  min %.5f  median %.5f  p99 %.5f  max %.5f"
          % (lum[0], lum[n // 2], lum[int(n * 0.99)], lum[-1]))


main()
