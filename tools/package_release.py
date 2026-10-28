# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""Package a firmware build into release files (used by CI, works the same locally).

  python tools/package_release.py --version v1.0.0 --uf2 build/sc26_gif.uf2 --out dist

Writes to --out:
  sc26-gif-player-<version>.uf2           the firmware: drop onto the RP2350 boot drive
  sc26-demo-gifs-<version>.uf2            optional: the animations in gifs/ as a GIF drive image
  sc26-gif-player-<version>-licenses.zip  LICENSE, THIRD_PARTY_NOTICES.md and every license text
  SHA256SUMS                              checksums of the three files above
  release-notes.md                        filled-in .github/release-notes.md (not a release asset)
"""
import argparse
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
# License files shipped next to binaries; paths match the links in THIRD_PARTY_NOTICES.md.
LICENSE_FILES = ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'lib/AnimatedGIF/LICENSE', 'lib/fatfs/LICENSE.txt']


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--version', required=True, help='e.g. v1.0.0 (the git tag)')
    ap.add_argument('--uf2', required=True, help='the built firmware, e.g. build/sc26_gif.uf2')
    ap.add_argument('--out', default='dist')
    a = ap.parse_args()

    if not re.fullmatch(r'[A-Za-z0-9._+-]+', a.version):
        sys.exit('version may only contain letters, digits and . _ + -')
    with open(a.uf2, 'rb') as f:
        head = f.read(512)
    if len(head) < 512 or head[:8] != b'UF2\n\x57\x51\x5d\x9e':
        sys.exit('%s is not a UF2 file' % a.uf2)

    os.makedirs(a.out, exist_ok=True)
    firmware = os.path.join(a.out, 'sc26-gif-player-%s.uf2' % a.version)
    demo = os.path.join(a.out, 'sc26-demo-gifs-%s.uf2' % a.version)
    licenses = os.path.join(a.out, 'sc26-gif-player-%s-licenses.zip' % a.version)

    shutil.copyfile(a.uf2, firmware)

    gifs = sorted(glob.glob(os.path.join(ROOT, 'gifs', '*.gif')))
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gif2uf2.py'), demo, *gifs], check=True,
                   stdout=subprocess.DEVNULL)

    with zipfile.ZipFile(licenses, 'w', zipfile.ZIP_DEFLATED) as z:
        for rel in LICENSE_FILES + sorted(os.path.relpath(p, ROOT).replace(os.sep, '/')
                                          for p in glob.glob(os.path.join(ROOT, 'LICENSES', '*'))):
            z.write(os.path.join(ROOT, rel), rel)

    assets = [firmware, demo, licenses]
    with open(os.path.join(a.out, 'SHA256SUMS'), 'w', newline='\n') as f:
        for p in assets:
            f.write('%s  %s\n' % (sha256(p), os.path.basename(p)))

    with open(os.path.join(ROOT, '.github', 'release-notes.md'), encoding='utf-8') as f:
        notes = f.read().replace('{{VERSION}}', a.version).replace('{{GIF_COUNT}}', str(len(gifs)))
    with open(os.path.join(a.out, 'release-notes.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(notes)

    for p in assets:
        print('%-46s %8d bytes  sha256 %s' % (os.path.basename(p), os.path.getsize(p), sha256(p)[:16]))


if __name__ == '__main__':
    main()
