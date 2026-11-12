#!/usr/bin/env python3
"""Draws the Mirage app icon (resources/icon/Mirage.png and Mirage.icns).

"Mi" drawn as shapes traced from the Mirage wordmark on a DSK-8 panel (heavy geometric
capitals: vertical outer stems, a deep V, a square i-dot set well apart), the grey and red
stripes from the same panel, and "SAMPLER" in the panel's small spaced white capitals.
usage: make_icon.py <sans font for SAMPLER> <out dir>
"""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SANS, OUT = sys.argv[1], sys.argv[2]
S = 1024                      # canvas
BODY, R = 824, 185            # macOS icon body and corner radius
X0 = (S - BODY) // 2
YELLOW = (244, 196, 12)
GREY = (200, 202, 206)
RED = (214, 34, 30)
WHITE = (236, 236, 236)

def icon(scale=4):
    W = S * scale
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    # drop shadow
    sh = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([X0 * scale, (X0 + 12) * scale, (X0 + BODY) * scale, (X0 + BODY + 12) * scale],
                                         R * scale, fill=(0, 0, 0, 110))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(14 * scale)))
    # panel: dark charcoal with a gentle vertical gradient
    panel = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    grad = Image.new("RGBA", (W, W))
    gd = ImageDraw.Draw(grad)
    for y in range(W):
        t = y / W
        c = tuple(int(a + (b - a) * t) for a, b in zip((58, 58, 62), (30, 30, 33)))
        gd.line([(0, y), (W, y)], fill=c + (255,))
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).rounded_rectangle([X0 * scale, X0 * scale, (X0 + BODY) * scale, (X0 + BODY) * scale], R * scale, fill=255)
    panel.paste(grad, (0, 0), mask)
    img.alpha_composite(panel)
    d = ImageDraw.Draw(img)

    # "Mi": outlines in units of the cap height H (traced from the wordmark)
    M = [(0, 0), (0.50, 0), (0.80, 0.60), (1.10, 0), (1.60, 0), (1.60, 1), (1.30, 1),
         (1.30, 0.36), (0.95, 0.93), (0.65, 0.93), (0.30, 0.36), (0.30, 1), (0, 1)]
    I_STEM = [(1.72, 0.24), (2.02, 0.24), (2.02, 1), (1.72, 1)]
    I_DOT = [(1.72, -0.12), (2.02, -0.12), (2.02, 0.12), (1.72, 0.12)]
    H = 300 * scale
    left = (W - 2.02 * H) / 2
    top = 208 * scale
    letters = Image.new("L", (W, W), 0)
    ld = ImageDraw.Draw(letters)
    for shape in (M, I_STEM, I_DOT):
        ld.polygon([(left + x * H, top + y * H) for x, y in shape], fill=255)
    # soften the corners slightly, as on the printed panel
    letters = letters.filter(ImageFilter.GaussianBlur(5 * scale)).point(lambda v: 255 if v > 128 else 0)
    letters = letters.filter(ImageFilter.GaussianBlur(scale * 0.7))
    yellow = Image.new("RGBA", (W, W), YELLOW + (255,))
    img.paste(yellow, (0, 0), letters)
    d = ImageDraw.Draw(img)

    # stripes: grey above red, across the panel
    lx0, lx1 = (X0 + 0) * scale, (X0 + BODY) * scale
    gy = 588 * scale
    d.rectangle([lx0, gy, lx1, gy + 11 * scale], fill=GREY)
    ry = gy + 30 * scale
    d.rectangle([lx0, ry, lx1, ry + 22 * scale], fill=RED)

    # SAMPLER, small spaced capitals
    fs = ImageFont.truetype(SANS, 58 * scale)
    word, spacing = "SAMPLER", 22 * scale
    widths = [d.textlength(ch, font=fs) for ch in word]
    total = sum(widths) + spacing * (len(word) - 1)
    x = (W - total) / 2
    y = ry + 22 * scale + 52 * scale
    for ch, w in zip(word, widths):
        d.text((x, y), ch, font=fs, fill=WHITE)
        x += w + spacing

    # clip everything to the body (stripes run to the edges)
    out = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    out.alpha_composite(img)
    body = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    body.paste(img, (0, 0), mask)
    shadow_only = img.copy()
    shadow_only.putalpha(Image.eval(shadow_only.getchannel("A"), lambda a: a) )
    final = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    final.alpha_composite(sh.filter(ImageFilter.GaussianBlur(14 * scale)))
    final.alpha_composite(body)
    return final.resize((S, S), Image.LANCZOS)

im = icon()
im.save(f"{OUT}/Mirage.png")
im.save(f"{OUT}/Mirage.icns", sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
print("wrote", OUT)
