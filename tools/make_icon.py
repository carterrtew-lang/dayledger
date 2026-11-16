#!/usr/bin/env python3
"""Regenerates src/app.ico, the DayLedger application icon (requires Pillow).

The artwork matches the logo drawn in the app: an indigo-to-violet rounded square holding a
day ring (white, with an amber "play" arc) and clock hands. Every size is rendered from an 8x
supersampled master so small sizes stay crisp.
"""
import math
import pathlib
from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
OUT = pathlib.Path(__file__).resolve().parents[1] / "src" / "app.ico"


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def render(size):
    k = 8
    n = size * k
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    # diagonal gradient body
    grad = Image.new("RGBA", (n, n))
    px = grad.load()
    start, end = (84, 104, 255), (139, 92, 246)
    for y in range(n):
        for x in range(n):
            px[x, y] = lerp(start, end, (x + y) / (2 * n - 2)) + (255,)
    mask = Image.new("L", (n, n), 0)
    inset = max(0, round(n * (0.02 if size >= 32 else 0.0)))
    ImageDraw.Draw(mask).rounded_rectangle([inset, inset, n - 1 - inset, n - 1 - inset],
                                           radius=round(n * 0.24), fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    c = n / 2
    r = n * (0.30 if size >= 24 else 0.31)
    w = n * (0.085 if size >= 32 else 0.11)
    box = [c - r, c - r, c + r, c + r]
    white, amber = (255, 255, 255, 255), (255, 196, 120, 255)
    # PIL angles: degrees clockwise from 3 o'clock, like GDI+.
    d.arc(box, -90, 160, fill=white, width=round(w))
    d.arc(box, 172, 258, fill=amber, width=round(w))
    hand = round(w * 0.95)
    d.line([(c, c), (c, c - r * 0.55)], fill=white, width=hand)
    d.line([(c, c), (c + r * 0.42, c + r * 0.18)], fill=white, width=hand)
    for x, y in [(c, c), (c, c - r * 0.55), (c + r * 0.42, c + r * 0.18)]:
        d.ellipse([x - hand / 2, y - hand / 2, x + hand / 2, y + hand / 2], fill=white)
    return img.resize((size, size), Image.LANCZOS)


def main():
    images = [render(s) for s in SIZES]
    images[-1].save(OUT, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
