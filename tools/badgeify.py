# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""Convert any animated GIF into one tailored for the SC26 badge's 360x360 round screen.

  python badgeify.py in.gif out.gif            # crop the centre square, fill the circle (default)
  python badgeify.py in.gif out.gif --fit      # keep the whole frame, fitted inside the circle
  python badgeify.py in.gif out.gif --focus 0.3 --colors 96 --max-frames 80

The firmware plays plain GIFs too; this just makes them look better and copy faster: frames are
fully composited, resized with a high-quality filter, the corners outside the circle are blacked
out (they are invisible and compress to nothing), and the palette is trimmed.
"""
import argparse
import os

from PIL import Image, ImageChops, ImageDraw, ImageSequence

SIZE = 360


def frames_of(path):
    im = Image.open(path)
    for f in ImageSequence.Iterator(im):
        yield f.convert('RGBA'), max(20, f.info.get('duration', im.info.get('duration', 100)) or 100)


def fit_frame(rgba, mode, focus, aspect=None):
    w, h = rgba.size
    if aspect and w / h > aspect:  # trim the sides of a very wide frame before fitting it
        nw = round(h * aspect)
        left = int((w - nw) * focus)
        rgba = rgba.crop((left, 0, left + nw, h))
        w = nw
    bg = Image.new('RGB', (SIZE, SIZE), 0)
    if mode == 'fill':
        side = min(w, h)
        left = int((w - side) * focus) if w > h else 0
        top = int((h - side) * focus) if h > w else 0
        sq = rgba.crop((left, top, left + side, top + side)).resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        bg.paste(sq, (0, 0), sq)
    else:
        # largest rectangle with this aspect that fits inside the circle (10% corner clipping allowed)
        s = min(SIZE / w, SIZE / h, 1.1 * SIZE / (w * w + h * h) ** 0.5)
        nw, nh = max(1, round(w * s)), max(1, round(h * s))
        sc = rgba.resize((nw, nh), Image.Resampling.LANCZOS)
        bg.paste(sc, ((SIZE - nw) // 2, (SIZE - nh) // 2), sc)
    return bg


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--fit', action='store_true', help='show the whole frame inside the circle instead of cropping')
    ap.add_argument('--focus', type=float, default=0.5, help='crop position along the long side, 0..1 (default centre)')
    ap.add_argument('--aspect', help='with --fit: first trim wider frames to this aspect, e.g. 16:10 (keeps a centred caption, shows it bigger)')
    ap.add_argument('--colors', type=int, default=96)
    ap.add_argument('--dither', action='store_true', help='smoother gradients, but a much bigger file')
    ap.add_argument('--max-frames', type=int, default=120, help='drop frames evenly above this many')
    ap.add_argument('--no-circle', action='store_true', help='keep the corners')
    ap.add_argument('--fuzz', type=int, default=8,
                    help='brightness change (0-255) below which a pixel counts as unchanged; -1 stores full frames')
    a = ap.parse_args()

    src = list(frames_of(a.src))
    step = max(1, -(-len(src) // a.max_frames))
    picked = []
    for i in range(0, len(src), step):
        frame, _ = src[i]
        picked.append((frame, sum(d for _, d in src[i:i + step])))

    circle = Image.new('L', (SIZE, SIZE), 0)
    ImageDraw.Draw(circle).ellipse((0, 0, SIZE - 1, SIZE - 1), fill=255)
    aspect = None
    if a.aspect:
        num, den = (float(v) for v in a.aspect.split(':'))
        aspect = num / den
    rgb = []
    for frame, _ in picked:
        im = fit_frame(frame, 'fit' if a.fit else 'fill', a.focus, aspect)
        if not a.no_circle:
            im = Image.composite(im, Image.new('RGB', im.size, 0), circle)
        rgb.append(im)

    # One palette for the whole clip (from a strip of sampled frames), with one spare index that
    # marks "unchanged since the previous frame" as transparent. Frames are kept in place
    # (disposal 1), so each frame only stores what visibly changed - like GIPHY/gifsicle do.
    samples = rgb[::max(1, len(rgb) // 12)]
    strip = Image.new('RGB', (SIZE, SIZE * len(samples)))
    for i, im in enumerate(samples):
        strip.paste(im, (0, i * SIZE))
    pal_img = strip.quantize(colors=a.colors - 1, method=Image.Quantize.MEDIANCUT)
    clear = a.colors - 1
    dither = Image.Dither.FLOYDSTEINBERG if a.dither else Image.Dither.NONE
    out, shown = [], None
    for im in rgb:
        q = im.quantize(palette=pal_img, dither=dither)
        if shown is None or a.fuzz < 0:
            frame = q
            shown = q.convert('RGB')
        else:
            cur = q.convert('RGB')
            changed = ImageChops.difference(cur, shown).convert('L').point(lambda v: 255 if v > a.fuzz else 0)
            frame = Image.composite(q, Image.new('P', q.size, clear), changed)
            shown = Image.composite(cur, shown, changed)
        frame.putpalette(pal_img.getpalette()[:3 * (a.colors - 1)] + [0, 0, 0])
        out.append(frame)
    out[0].save(a.dst, save_all=True, append_images=out[1:], duration=[d for _, d in picked], loop=0, disposal=1,
                transparency=clear, optimize=False)
    print('%s: %d -> %d frames, %.1fs loop, %d KB' % (os.path.basename(a.dst), len(src), len(out),
                                                       sum(d for _, d in picked) / 1000, os.path.getsize(a.dst) // 1024))


if __name__ == '__main__':
    main()
