# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""Generate original test animations for the SC26 badge (no third-party artwork).

  matrix_rain.gif   - "digital rain" in the style of the film's title sequence, 360x360, seamless loop
  matrix_small.gif  - same effect at 180x180 (the firmware scales it 2x); small enough to embed
  sc26_rings.gif    - pulsing colour rings sized for the round panel

usage: python make_test_gifs.py <outdir>
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

FRAMES = 48   # every column speed divides this, so the animation loops seamlessly
DELAY_MS = 60


def make_glyphs(n, w, h, seed=7):
    """Random, roughly katakana-looking bitmaps: a few strokes on a w x h grid, mirrored like the film's glyphs."""
    rnd = random.Random(seed)
    glyphs = []
    for _ in range(n):
        g = Image.new('1', (w, h), 0)
        d = ImageDraw.Draw(g)
        for _ in range(rnd.randint(2, 4)):
            kind = rnd.random()
            if kind < 0.4:    # horizontal stroke
                y = rnd.randrange(h)
                x0 = rnd.randrange(w // 2)
                d.line([(x0, y), (rnd.randrange(x0 + 1, w), y)], fill=1)
            elif kind < 0.75:  # vertical stroke
                x = rnd.randrange(w)
                y0 = rnd.randrange(h // 2)
                d.line([(x, y0), (x, rnd.randrange(y0 + 1, h))], fill=1)
            else:              # diagonal
                d.line([(rnd.randrange(w), rnd.randrange(h)), (rnd.randrange(w), rnd.randrange(h))], fill=1)
        glyphs.append(g.transpose(Image.Transpose.FLIP_LEFT_RIGHT))
    return glyphs


def rain_palette():
    # index 0 black, 1..14 dark->bright green trail, 15 white-green head
    pal = [0, 0, 0]
    for i in range(1, 15):
        t = i / 14
        pal += [int(10 * t), int(40 + 215 * t ** 1.3), int(15 * t)]
    pal += [210, 255, 210]
    return pal + [0] * (768 - len(pal))


def matrix_rain(size, cell_w, cell_h, glyph_scale, seed=1):
    rnd = random.Random(seed)
    cols, rows = size // cell_w, size // cell_h + 1
    span = FRAMES  # head position wraps every `span` rows of travel
    glyphs = make_glyphs(64, 5, 7)
    columns = []
    for c in range(cols):
        columns.append({
            'speed': rnd.choice([1, 1, 2, 2, 3]) if rnd.random() < 0.9 else 4,
            'offset': rnd.randrange(span),
            'trail': rnd.randint(8, 20),
            'cells': [rnd.randrange(len(glyphs)) for _ in range(rows)],
            'flicker': [rnd.randrange(FRAMES) for _ in range(rows)],
        })
    frames = []
    for t in range(FRAMES):
        im = Image.new('P', (size, size), 0)
        im.putpalette(rain_palette())
        for c, col in enumerate(columns):
            head = (col['offset'] + t * col['speed']) % span
            for r in range(rows):
                dist = head - r
                if dist < 0 or dist > col['trail']:
                    continue
                shade = 15 if dist == 0 else max(1, 14 - int(13 * dist / col['trail']))
                gi = col['cells'][r]
                if (t + col['flicker'][r]) % 12 == 0:  # occasional glyph swap, periodic so it loops
                    gi = (gi + t) % len(glyphs)
                g = glyphs[gi].resize((5 * glyph_scale, 7 * glyph_scale), Image.Resampling.NEAREST)
                x = c * cell_w + (cell_w - g.width) // 2
                y = r * cell_h + (cell_h - g.height) // 2
                im.paste(shade, (x, y), g)
        frames.append(im)
    return frames


def rings(size):
    frames = []
    cx = cy = size / 2
    for t in range(FRAMES):
        im = Image.new('RGB', (size, size), 0)
        px = im.load()
        for y in range(size):
            for x in range(size):
                r = math.hypot(x - cx, y - cy)
                if r > size / 2:
                    continue
                v = math.sin(r / 9 - t * 2 * math.pi / FRAMES * 3)
                hue = (r / (size / 2) + t / FRAMES) % 1.0
                k = 0.5 + 0.5 * v
                px[x, y] = tuple(int(255 * k * (0.5 + 0.5 * math.cos(2 * math.pi * (hue + o)))) for o in (0, 1 / 3, 2 / 3))
        frames.append(im.quantize(colors=64, method=Image.Quantize.MEDIANCUT))
    return frames


def save(frames, path):
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=DELAY_MS, loop=0, optimize=False, disposal=1)
    print('%-40s %3d frames  %7d bytes' % (path, len(frames), os.path.getsize(path)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    save(matrix_rain(360, 12, 16, 2), os.path.join(out, 'matrix_rain.gif'))
    save(matrix_rain(180, 6, 9, 1, seed=3), os.path.join(out, 'matrix_small.gif'))
    save(rings(180), os.path.join(out, 'sc26_rings.gif'))


if __name__ == '__main__':
    main()
