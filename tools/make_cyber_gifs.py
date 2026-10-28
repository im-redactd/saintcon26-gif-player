# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""Original hacker-conference animations for the SC26 badge's 360x360 round screen.

  hack_the_planet.gif - spinning wireframe globe with a glitching "HACK THE PLANET!"
  always_dns.gif      - the sysadmin haiku, typed out on a terminal
  access_granted.gif  - fake recon/cracking terminal session ending in ACCESS GRANTED
  radar.gif           - radar sweep picking up blips (made for a round display)

Everything important stays inside the visible circle (radius ~170 px).
usage: python make_cyber_gifs.py <outdir> [regular.ttf bold.ttf]

The committed GIFs were rendered with Consolas; without font arguments the script looks for
Consolas (Windows), Menlo (macOS) or DejaVu Sans Mono (Linux).
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

SIZE = 360
C = SIZE // 2
GREEN, CYAN, MAGENTA, RED, AMBER = (40, 255, 90), (0, 230, 255), (255, 40, 200), (255, 50, 50), (255, 190, 40)

FONT_CANDIDATES = {  # (regular, bold), first match wins
    'regular': [r'C:\Windows\Fonts\consola.ttf', '/System/Library/Fonts/Menlo.ttc',
                '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', '/usr/share/fonts/TTF/DejaVuSansMono.ttf'],
    'bold': [r'C:\Windows\Fonts\consolab.ttf', '/System/Library/Fonts/Menlo.ttc',
             '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', '/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf'],
}
FONT_ARGS = {'regular': sys.argv[2], 'bold': sys.argv[3]} if len(sys.argv) > 3 else {}


def font(weight, size):
    paths = [FONT_ARGS[weight]] if weight in FONT_ARGS else FONT_CANDIDATES[weight]
    for path in paths:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    sys.exit('no monospace %s font found; pass regular and bold .ttf paths after the output dir' % weight)


def save(frames, durations, path, colors=64):
    pal = [f.convert('RGB').quantize(colors=colors, method=Image.Quantize.MEDIANCUT) for f in frames]
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=durations, loop=0, disposal=1)
    print('%-22s %3d frames %8d bytes' % (os.path.basename(path), len(frames), os.path.getsize(path)))


def round_mask(im):
    """Black outside the visible circle (keeps files small and shows what the badge shows)."""
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).ellipse((1, 1, SIZE - 2, SIZE - 2), fill=255)
    return Image.composite(im, Image.new('RGB', im.size, 0), m)


def centered(draw, y, text, f, fill):
    w = draw.textlength(text, font=f)
    draw.text((C - w / 2, y), text, font=f, fill=fill)


def glitch(im, t, strength):
    """Horizontal slice offsets plus an RGB channel split."""
    rnd = random.Random(t * 7919)
    out = im.copy()
    for _ in range(strength):
        y = rnd.randrange(SIZE - 12)
        h = rnd.randint(3, 14)
        dx = rnd.randint(-18, 18)
        band = im.crop((0, y, SIZE, y + h))
        out.paste(ImageChops.offset(band, dx, 0), (0, y))
    r, g, b = out.split()
    return Image.merge('RGB', (ImageChops.offset(r, 3, 0), g, ImageChops.offset(b, -3, 0)))


def hack_the_planet(out):
    frames, n = [], 36
    title = font('bold', 30)
    sub = font('regular', 15)
    R = 110
    tilt = math.radians(23)
    for t in range(n):
        im = Image.new('RGB', (SIZE, SIZE), 0)
        d = ImageDraw.Draw(im)
        spin = 2 * math.pi * (t / n) / 12  # one meridian spacing per loop: seamless with 12 meridians

        def project(lat, lon):
            x = math.cos(lat) * math.cos(lon + spin)
            y = math.sin(lat)
            z = math.cos(lat) * math.sin(lon + spin)
            y, z = y * math.cos(tilt) - z * math.sin(tilt), y * math.sin(tilt) + z * math.cos(tilt)
            return C + R * x, C - 12 - R * y, z

        def line(points):
            for (x0, y0, z0), (x1, y1, z1) in zip(points, points[1:]):
                front = (z0 + z1) / 2 > 0
                d.line([(x0, y0), (x1, y1)], fill=GREEN if front else (0, 70, 30), width=2 if front else 1)

        for k in range(12):
            lon = 2 * math.pi * k / 12
            line([project(math.radians(a), lon) for a in range(-90, 91, 6)])
        for lat in range(-60, 61, 30):
            line([project(math.radians(lat), math.radians(a)) for a in range(0, 361, 6)])
        d.ellipse((C - R, C - 12 - R, C + R, C - 12 + R), outline=CYAN, width=1)
        centered(d, 262, 'HACK THE PLANET!', title, MAGENTA)
        centered(d, 300, '// SC26 //', sub, CYAN)
        if t % 12 in (5, 6) or t == 20:
            im = glitch(im, t, 9)
        frames.append(round_mask(im))
    save(frames, [70] * n, os.path.join(out, 'hack_the_planet.gif'))


def terminal_frame(lines, cursor_on, f, color=GREEN, top=78, left=58, line_h=24):
    im = Image.new('RGB', (SIZE, SIZE), (2, 8, 4))
    d = ImageDraw.Draw(im)
    for i, (text, col) in enumerate(lines):
        d.text((left, top + i * line_h), text, font=f, fill=col or color)
    if cursor_on and lines:
        last, _ = lines[-1]
        x = left + d.textlength(last, font=f)
        y = top + (len(lines) - 1) * line_h
        d.rectangle((x + 2, y + 3, x + 11, y + 20), fill=color)
    return im


def always_dns(out):
    f = font('regular', 19)
    big = font('bold', 34)
    script = [('$ cat haiku.txt', None), ('', None), ("It's not DNS", None), ("There's no way it's DNS", None),
              ('It was DNS', RED)]
    frames, durs = [], []
    shown = []
    for text, col in script:
        for i in range(0, len(text) + 1, 2):
            frames.append(round_mask(terminal_frame(shown + [(text[:i], col)], True, f, top=96)))
            durs.append(55)
        shown.append((text, col))
        for blink in range(4 if text else 1):
            frames.append(round_mask(terminal_frame(shown, blink % 2 == 0, f, top=96)))
            durs.append(220)
    for k in range(6):
        im = terminal_frame(shown, False, f, top=96)
        d = ImageDraw.Draw(im)
        if k % 2 == 0:
            d.rectangle((70, 230, 290, 280), fill=RED)
            centered(d, 236, "IT'S DNS", big, (0, 0, 0))
        frames.append(round_mask(im))
        durs.append(450)
    save(frames, durs, os.path.join(out, 'always_dns.gif'), colors=16)


def access_granted(out):
    f = font('regular', 16)
    big = font('bold', 30)
    rnd = random.Random(26)
    log = ['$ nmap -sV 10.26.0.0/24', '22/tcp  open  ssh', '80/tcp  open  http', '443/tcp open  https',
           '$ ./exploit --target gibson', '[*] sending payload...', '[+] shell spawned', '$ crack --hash ntlm admin']
    frames, durs = [], []
    shown = []
    for line in log:
        shown.append((line, AMBER if line.startswith('$') else None))
        frames.append(round_mask(terminal_frame(shown[-8:], True, f, top=84, left=60, line_h=21)))
        durs.append(260 if line.startswith('$') else 120)
    for p in range(0, 101, 5):
        bar = '[' + '#' * (p // 5) + '.' * (20 - p // 5) + '] %3d%%' % p
        guess = ''.join(rnd.choice('abcdef0123456789') for _ in range(16))
        lines = shown[-6:] + [(bar, CYAN), ('try ' + guess, (0, 120, 50))]
        frames.append(round_mask(terminal_frame(lines, False, f, top=84, left=60, line_h=21)))
        durs.append(70)
    for k in range(8):
        im = terminal_frame(shown[-6:] + [('[' + '#' * 20 + '] 100%', CYAN), ('password: ********', None)], False, f,
                            top=84, left=60, line_h=21)
        d = ImageDraw.Draw(im)
        if k % 2 == 0:
            d.rectangle((50, 150, 310, 230), fill=(0, 0, 0), outline=GREEN, width=3)
            centered(d, 172, 'ACCESS GRANTED', big, GREEN)
        frames.append(round_mask(im))
        durs.append(400)
    save(frames, durs, os.path.join(out, 'access_granted.gif'), colors=24)


def radar(out):
    n = 48
    f = font('regular', 13)
    rnd = random.Random(3)
    blips = [(rnd.uniform(0, 2 * math.pi), rnd.uniform(40, 150), name) for name in
             ('BLE', 'WiFi', 'SC26', 'NFC', 'LoRa', 'BLE', 'WiFi', '???')]
    frames = []
    for t in range(n):
        im = Image.new('RGB', (SIZE, SIZE), (0, 12, 4))
        d = ImageDraw.Draw(im)
        for r in (40, 80, 120, 160):
            d.ellipse((C - r, C - r, C + r, C + r), outline=(0, 90, 30), width=1)
        d.line((C - 168, C, C + 168, C), fill=(0, 70, 25))
        d.line((C, C - 168, C, C + 168), fill=(0, 70, 25))
        sweep = 2 * math.pi * t / n
        for k in range(14):  # fading trail behind the beam
            a = sweep - k * 0.045
            shade = int(200 * (1 - k / 14))
            d.line((C, C, C + 168 * math.cos(a), C + 168 * math.sin(a)), fill=(0, shade, shade // 4), width=3)
        for ang, dist, name in blips:
            age = (sweep - ang) % (2 * math.pi)  # time since the beam passed the blip
            if age < 2 * math.pi * 0.7:
                k = 1 - age / (2 * math.pi * 0.7)
                x, y = C + dist * math.cos(ang), C + dist * math.sin(ang)
                col = (int(80 * k), int(255 * k), int(120 * k))
                d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=col)
                if k > 0.45:
                    d.text((x + 8, y - 7), name, font=f, fill=col)
        frames.append(round_mask(im))
    save(frames, [60] * n, os.path.join(out, 'radar.gif'))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    hack_the_planet(out)
    always_dns(out)
    access_granted(out)
    radar(out)


if __name__ == '__main__':
    main()
