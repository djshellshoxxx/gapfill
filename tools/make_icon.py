"""Generates the GapFill application/plugin icon as PNG (no third-party deps).

The icon shows a row of tall "event" bars with smaller accent "fill" bars living
in the spaces between them - the plugin's core idea in one image.
"""
import math, struct, zlib, os

SS = 3  # supersampling factor


def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def rounded_rect_cov(px, py, x0, y0, x1, y1, r):
    """Coverage test for a point inside a rounded rectangle."""
    if px < x0 or px > x1 or py < y0 or py > y1:
        return False
    cx = min(max(px, x0 + r), x1 - r)
    cy = min(max(py, y0 + r), y1 - r)
    return (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def make(size, path):
    w = h = size
    acc = [[[0.0, 0.0, 0.0, 0.0] for _ in range(w)] for _ in range(h)]

    s = size / 512.0
    pad = 26 * s
    radius = 104 * s

    # Bars: (x_centre, half_width, half_height, colour, is_event)
    bars = []
    ev_color = (0.92, 0.96, 1.0)
    fill_a = (1.00, 0.42, 0.72)
    fill_b = (0.35, 0.92, 0.98)
    fill_c = (1.00, 0.78, 0.30)

    cx0 = 96 * s
    step = 106 * s
    for i in range(4):
        bars.append((cx0 + i * step, 15 * s, (150 - 14 * abs(1.5 - i)) * s, ev_color, True))

    # Fill accents in the gaps
    gaps = [
        (cx0 + 0.5 * step, [(-26, 9, 54, fill_a), (0, 7, 30, fill_b), (26, 9, 72, fill_c)]),
        (cx0 + 1.5 * step, [(-30, 7, 34, fill_b), (-8, 9, 86, fill_a), (18, 7, 46, fill_c), (38, 6, 22, fill_b)]),
        (cx0 + 2.5 * step, [(-22, 8, 64, fill_c), (6, 10, 38, fill_a), (30, 7, 92, fill_b)]),
    ]
    for gx, items in gaps:
        for dx, hw, hh, col in items:
            bars.append((gx + dx * s, hw * s, hh * s, col, False))

    ycen = h * 0.53
    bar_r = 7 * s

    for yy in range(h):
        row = acc[yy]
        for xx in range(w):
            r = g = b = a = 0.0
            for sy in range(SS):
                py = yy + (sy + 0.5) / SS
                for sx in range(SS):
                    px = xx + (sx + 0.5) / SS
                    if not rounded_rect_cov(px, py, pad, pad, w - pad, h - pad, radius):
                        continue
                    # Background gradient (deep blue -> near black violet)
                    t = clamp01((px + py) / (w + h))
                    br = 0.055 + 0.045 * (1.0 - t)
                    bg = 0.070 + 0.075 * (1.0 - t)
                    bb = 0.110 + 0.150 * (1.0 - t)
                    # Subtle "gap" highlight band behind the centre
                    band = math.exp(-((py - ycen) ** 2) / (2 * (95 * s) ** 2))
                    br += 0.035 * band
                    bg += 0.045 * band
                    bb += 0.085 * band
                    cr, cg, cb, ca = br, bg, bb, 1.0
                    for bx, bhw, bhh, col, is_ev in bars:
                        if rounded_rect_cov(px, py, bx - bhw, ycen - bhh, bx + bhw, ycen + bhh, min(bar_r, bhw)):
                            glow = 1.0 if is_ev else 1.0
                            cr, cg, cb = col[0] * glow, col[1] * glow, col[2] * glow
                    r += cr * ca
                    g += cg * ca
                    b += cb * ca
                    a += ca
            n = SS * SS
            row[xx] = [r / n, g / n, b / n, a / n]

    raw = bytearray()
    for yy in range(h):
        raw.append(0)
        for xx in range(w):
            r, g, b, a = acc[yy][xx]
            if a > 1e-6:
                r, g, b = r / a, g / a, b / a
            raw += bytes((int(clamp01(r) * 255 + 0.5), int(clamp01(g) * 255 + 0.5),
                          int(clamp01(b) * 255 + 0.5), int(clamp01(a) * 255 + 0.5)))

    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)
    print('wrote', path, os.path.getsize(path), 'bytes')


if __name__ == '__main__':
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    res = os.path.join(here, 'Resources')
    os.makedirs(res, exist_ok=True)
    make(512, os.path.join(res, 'icon.png'))
    make(128, os.path.join(res, 'icon_small.png'))
