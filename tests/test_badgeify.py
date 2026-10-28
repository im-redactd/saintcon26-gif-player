# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""badgeify: output is a 360x360 looping GIF that looks like the input (frame differencing must
not change what is displayed).

Run from the repository root: python -m unittest discover -s tests -v
"""
import os
import subprocess
import sys
import tempfile
import unittest

from PIL import Image, ImageChops, ImageDraw, ImageStat

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'badgeify.py')


def moving_square_gif(path, w=320, h=180, frames=12):
    imgs = []
    for t in range(frames):
        im = Image.new('RGB', (w, h), (20, 40, 90))
        d = ImageDraw.Draw(im)
        d.rectangle((t * 20, 60, t * 20 + 50, 110), fill=(250, 200, 30))
        imgs.append(im)
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=80, loop=0)


def frames(path):
    out = []
    with Image.open(path) as im:
        for i in range(im.n_frames):
            im.seek(i)
            out.append((im.convert('RGB'), im.info.get('duration')))
    return out


class BadgeifyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.src = os.path.join(self.tmp.name, 'src.gif')
        moving_square_gif(self.src)

    def tearDown(self):
        self.tmp.cleanup()

    def convert(self, *extra):
        out = os.path.join(self.tmp.name, 'out.gif')
        r = subprocess.run([sys.executable, TOOL, self.src, out, *extra], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return out

    def test_size_frames_and_timing(self):
        out = self.convert()
        got = frames(out)
        self.assertEqual(len(got), 12)
        self.assertTrue(all(f.size == (360, 360) for f, _ in got))
        self.assertTrue(all(d == 80 for _, d in got))

    def test_frame_differencing_is_invisible(self):
        delta = frames(self.convert('--fuzz', '0'))
        full = frames(self.convert('--fuzz', '-1'))
        for (a, _), (b, _) in zip(delta, full):
            worst = max(ImageStat.Stat(ImageChops.difference(a, b)).mean)
            self.assertLess(worst, 1.0)

    def test_corners_are_black(self):
        first, _ = frames(self.convert('--fit'))[0]
        for corner in [(0, 0), (359, 0), (0, 359), (359, 359)]:
            self.assertEqual(first.getpixel(corner), (0, 0, 0))

    def test_fit_keeps_whole_frame(self):
        first, _ = frames(self.convert('--fit'))[0]
        bbox = first.getbbox()  # non-black area = the fitted picture
        width = bbox[2] - bbox[0]
        self.assertGreater(width, 250)  # 320-wide source scaled into the circle, not cropped to 180


if __name__ == '__main__':
    unittest.main()
