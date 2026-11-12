"""Shared drawing of the Mirage wordmark letters, traced from the DSK-8 panel.

Coordinates are in units of the cap height H: y = 0 is the cap line, y = 1 the baseline,
x-height at 0.24 from the top. Letters are built from polygons and rounded rectangles
(positive shapes) minus counters and slots (negative shapes), rasterised with PIL.
"""
from PIL import Image, ImageDraw, ImageFilter

XH = 0.24      # top of the lower-case letters
ST = 0.30      # stem width

def _rr(d, x0, y0, x1, y1, r, H, ox, oy, fill):
    d.rounded_rectangle([ox + x0 * H, oy + y0 * H, ox + x1 * H, oy + y1 * H], r * H, fill=fill)

def _poly(d, pts, H, ox, oy, fill):
    d.polygon([(ox + x * H, oy + y * H) for x, y in pts], fill=fill)

def letters(text="Mirage"):
    """Returns a list of draw ops (kind, args, positive) and the total width in H."""
    ops, x = [], 0.0
    gap = 0.10
    for ch in text:
        if ch == "M":
            ops.append(("poly", [(x, 0), (x + 0.50, 0), (x + 0.80, 0.60), (x + 1.10, 0), (x + 1.60, 0), (x + 1.60, 1),
                                 (x + 1.30, 1), (x + 1.30, 0.36), (x + 0.95, 0.93), (x + 0.65, 0.93), (x + 0.30, 0.36),
                                 (x + 0.30, 1), (x, 1)], True))
            x += 1.60
        elif ch == "i":
            ops.append(("rr", (x, XH, x + ST, 1, 0.03), True))
            ops.append(("rr", (x, -0.12, x + ST, 0.12, 0.03), True))
            x += ST
        elif ch == "r":
            ops.append(("rr", (x, XH, x + ST, 1, 0.03), True))
            ops.append(("rr", (x, XH, x + 0.70, XH + 0.27, 0.13), True))
            ops.append(("rr", (x, XH + 0.13, x + 0.2, 1, 0.0), True))
            x += 0.70
        elif ch == "a":
            w = 0.98
            ops.append(("rr", (x, XH, x + w, 1, 0.22), True))
            ops.append(("rect", (x - 0.05, XH + 0.21, x + w - ST, XH + 0.31), False))   # slot open to the left
            ops.append(("rr", (x + 0.27, XH + 0.47, x + w - ST, 1 - 0.14, 0.05), False))  # bowl counter
            x += w
        elif ch == "g":
            w = 0.98
            ops.append(("rr", (x, XH, x + w, 0.98, 0.22), True))
            ops.append(("rr", (x + 0.27, XH + 0.21, x + w - ST, 0.98 - 0.21, 0.05), False))
            ops.append(("rect", (x + w - ST, 0.80, x + w, 1.12), True))                  # stem into the tail
            ops.append(("rr", (x + 0.06, 1.04, x + w, 1.30, 0.13), True))                # tail
            ops.append(("rect", (x - 0.05, 0.98, x + w - ST, 1.06), False))              # gap above the tail
            x += w
        elif ch == "e":
            w = 1.00
            ops.append(("rr", (x, XH, x + w, 1, 0.24), True))
            ops.append(("rr", (x + 0.27, XH + 0.17, x + w - 0.27, XH + 0.35, 0.07), False))  # upper counter
            ops.append(("rect", (x + 0.27, XH + 0.50, x + w + 0.05, XH + 0.60), False))      # slot open to the right
            x += w
        x += gap
    return ops, x - gap

def render(text, H, color, pad=0.2, blur=0.012):
    """RGBA image of `text`, cap height H pixels, padding in H."""
    ops, width = letters(text)
    W, Ht = int((width + 2 * pad) * H), int((1.30 + 0.12 + 2 * pad) * H)
    ox, oy = pad * H, (pad + 0.12) * H
    mask = Image.new("L", (W, Ht), 0)
    d = ImageDraw.Draw(mask)
    for kind, args, positive in ops:
        fill = 255 if positive else 0
        if kind == "poly":
            _poly(d, args, H, ox, oy, fill)
        elif kind == "rr":
            x0, y0, x1, y1, r = args
            _rr(d, x0, y0, x1, y1, r, H, ox, oy, fill)
        else:
            x0, y0, x1, y1 = args
            d.rectangle([ox + x0 * H, oy + y0 * H, ox + x1 * H, oy + y1 * H], fill=fill)
    # soften the corners slightly, as printed
    mask = mask.filter(ImageFilter.GaussianBlur(H * blur)).point(lambda v: 255 if v > 128 else 0)
    mask = mask.filter(ImageFilter.GaussianBlur(max(0.6, H * 0.002)))
    img = Image.new("RGBA", (W, Ht), color + (0,))
    img.putalpha(mask)
    return img
