# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""gif2uf2: the UF2 must decode back to a valid FAT16 volume holding exactly the input files.

Run from the repository root: python -m unittest discover -s tests -v
"""
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'gif2uf2.py')
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import gif2uf2  # noqa: E402

OFFSET = 0x100000


def tiny_gif(seed, frames=3):
    imgs = [Image.new('P', (24, 24), (seed + i) % 256) for i in range(frames)]
    for im in imgs:
        im.putpalette([v for c in range(256) for v in (c, 255 - c, (c * 7) % 256)])
    buf = io.BytesIO()
    imgs[0].save(buf, 'GIF', save_all=True, append_images=imgs[1:], duration=100, loop=0)
    return buf.getvalue()


def slurp(path):
    with open(path, 'rb') as f:
        return f.read()


def run_tool(out, paths, *extra):
    return subprocess.run([sys.executable, TOOL, out, *paths, *extra], capture_output=True, text=True)


def decode_uf2(path):
    """UF2 -> {flash offset: 256-byte page}, checking every block header."""
    data = slurp(path)
    pages = {}
    for i in range(0, len(data), 512):
        m0, m1, flags, addr, size, blockno, nblocks, family = struct.unpack_from('<8I', data, i)
        assert (m0, m1) == (0x0A324655, 0x9E5D5157) and struct.unpack_from('<I', data, i + 508)[0] == 0x0AB16F30
        assert flags == 0x2000 and family == gif2uf2.UF2_FAMILY_ABSOLUTE and size == 256
        assert blockno == i // 512 and nblocks == len(data) // 512
        pages[addr - gif2uf2.XIP_BASE] = data[i + 32:i + 32 + 256]
    return pages


def read_volume(pages):
    """Parse the FAT16 volume at OFFSET; returns {long name: bytes}."""
    end = max(pages) + 256
    img = bytearray(b'\xff' * (end - OFFSET))
    for addr, page in pages.items():
        if addr >= OFFSET:
            img[addr - OFFSET:addr - OFFSET + 256] = page
    bps, spc, rsv, nfat, nroot, ts16, media, fsz = struct.unpack_from('<HBHBHHBH', img, 11)
    assert img[510:512] == b'\x55\xaa' and img[54:62] == b'FAT16   ' and bps == 512 and nfat == 2
    fat = img[rsv * 512:(rsv + fsz) * 512]
    assert fat == img[(rsv + fsz) * 512:(rsv + 2 * fsz) * 512], 'FAT copies differ'
    root = (rsv + 2 * fsz) * 512
    data = root + nroot * 32
    files, lfn = {}, []
    for e in range(nroot):
        d = img[root + 32 * e:root + 32 * e + 32]
        if d[0] == 0:
            break
        if d[11] == 0x0F:
            lfn.insert(0, d)
            continue
        if d[11] & 0x08:  # volume label
            continue
        if lfn:
            csum = 0
            for c in d[:11]:
                csum = (((csum & 1) << 7) + (csum >> 1) + c) & 0xFF
            assert all(x[13] == csum for x in lfn), 'LFN checksum mismatch'
            raw = b''.join(bytes(x[1:11] + x[14:26] + x[28:32]) for x in lfn)
            name = raw.decode('utf-16-le').split('\0')[0].rstrip('￿')
        else:
            base, ext = d[:8].decode().rstrip(), d[8:11].decode().rstrip()
            name = base + ('.' + ext if ext else '')
        lfn = []
        cl, size = struct.unpack_from('<H', d, 26)[0], struct.unpack_from('<I', d, 28)[0]
        out = bytearray()
        while 2 <= cl < 0xFFF8:
            out += img[data + (cl - 2) * spc * 512:data + (cl - 1) * spc * 512]
            cl = struct.unpack_from('<H', fat, cl * 2)[0]
        files[name] = bytes(out[:size])
    return files


class Gif2Uf2Test(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, name, seed=1):
        path = os.path.join(self.dir, name)
        with open(path, 'wb') as f:
            f.write(tiny_gif(seed))
        return path

    def test_round_trip_with_long_and_unicode_names(self):
        names = ['matrix_rain.gif', 'café ☕.gif', 'abcdefghi.gif', 'UPPER.GIF', 'smile 😀.gif']
        paths = [self.make(n, i) for i, n in enumerate(names)]
        out = os.path.join(self.dir, 'out.uf2')
        r = run_tool(out, paths)
        self.assertEqual(r.returncode, 0, r.stderr)
        files = read_volume(decode_uf2(out))
        for n, p in zip(names, paths):
            self.assertEqual(files[n], slurp(p), n)
        self.assertIn('README.TXT', files)

    def test_whole_erase_sectors_and_journal_wiped(self):
        out = os.path.join(self.dir, 'out.uf2')
        self.assertEqual(run_tool(out, [self.make('a.gif')]).returncode, 0)
        pages = decode_uf2(out)
        for addr in pages:  # the boot ROM erases a 4 KB sector before programming it
            sector = addr - addr % 4096
            self.assertTrue(all(p in pages for p in range(sector, sector + 4096, 256)), hex(sector))
        journal = range(OFFSET - gif2uf2.JOURNAL_BYTES, OFFSET, 256)
        self.assertTrue(all(pages.get(p) == b'\xff' * 256 for p in journal))
        self.assertGreaterEqual(min(pages), OFFSET - gif2uf2.JOURNAL_BYTES, 'must not touch the firmware')

    def test_lfn_entry_count(self):
        sfn = gif2uf2.short_name('abcdefghi.gif', set())
        self.assertEqual(len(gif2uf2.lfn_entries('abcdefghi.gif', sfn)), 1)  # exactly 13 chars: no extra entry
        self.assertEqual(len(gif2uf2.lfn_entries('abcdefghij.gif', sfn)), 2)
        self.assertEqual(len(gif2uf2.short_name('ünïcødé.gif', set())), 11)

    def test_rejects_bad_input(self):
        out = os.path.join(self.dir, 'out.uf2')
        a, b = self.make('cat.gif'), os.path.join(self.dir, 'sub')
        os.mkdir(b)
        dup = os.path.join(b, 'CAT.gif')
        with open(dup, 'wb') as f:
            f.write(tiny_gif(9))
        self.assertNotEqual(run_tool(out, [a, dup]).returncode, 0)  # case-insensitive duplicate
        notgif = os.path.join(self.dir, 'x.gif')
        with open(notgif, 'wb') as f:
            f.write(b'not a gif')
        self.assertNotEqual(run_tool(out, [notgif]).returncode, 0)
        self.assertNotEqual(run_tool(out, [a], '--offset', '0x80000').returncode, 0)  # inside the firmware
        self.assertNotEqual(run_tool(out, [a], '--offset', '0x100800').returncode, 0)  # not 4 KB aligned


if __name__ == '__main__':
    unittest.main()
