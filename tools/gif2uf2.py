# SPDX-License-Identifier: MIT
# Copyright (c) 2026 redactd
"""Pack GIFs into a UF2 that loads them onto the SC26 badge from BOOTSEL (UF2) mode.

The RP2350 boot ROM's USB drive only accepts .uf2 files, but a UF2 can target any flash
address. This builds a complete FAT16 volume holding the given GIFs (long file names
included) and wraps it as a UF2 aimed at the badge's GIF-drive region, so dropping the
.uf2 on the RP2350 drive replaces the drive's contents and leaves the firmware alone.

usage: python gif2uf2.py out.uf2 a.gif [b.gif ...] [--offset 0x100000] [--flash-size 16M]
"""
import argparse
import os
import struct
import sys

SECTOR = 512
SECTORS_PER_CLUSTER = 4          # 2 KiB clusters keep a 15 MB volume FAT16 (FatFs mounts either)
ROOT_ENTRIES = 512
FLASH_ERASE = 4096
UF2_PAYLOAD = 256
UF2_FAMILY_ABSOLUTE = 0xE48BFF57  # RP2350: write at the absolute address given, no partition lookup
XIP_BASE = 0x10000000
MIN_DRIVE_OFFSET = 0x100000       # the firmware owns the first 1 MB
JOURNAL_BYTES = 8192              # firmware's metadata journal, right below the drive (see flash_disk.c)

README = (b"SC26 badge GIF drive\r\n====================\r\n\r\n"
          b"Loaded from BOOTSEL mode with gif2uf2.py. Copy more .gif files here while the\r\n"
          b"badge is running (it appears as a normal USB drive), or build a new UF2.\r\n")


def size_arg(s):
    s = s.strip().upper()
    mult = {'K': 1024, 'M': 1024 * 1024}.get(s[-1], 1)
    return int(s[:-1] if s[-1] in 'KM' else s, 0) * mult


def layout(total_sectors):
    """Pick reserved/FAT sizes so the data area starts on a 4 KiB flash erase boundary."""
    root_sectors = ROOT_ENTRIES * 32 // SECTOR
    for reserved in range(1, 64):
        clusters = (total_sectors - reserved - root_sectors) // SECTORS_PER_CLUSTER
        fat_sectors = -(-((clusters + 2) * 2) // SECTOR)
        data_start = reserved + 2 * fat_sectors + root_sectors
        if (data_start * SECTOR) % FLASH_ERASE:
            continue
        clusters = (total_sectors - data_start) // SECTORS_PER_CLUSTER
        if not 4087 <= clusters <= 65524:  # FatFs: <= 4085 is FAT12; Windows reportedly < 4087
            sys.exit('volume size gives %d clusters, not a FAT16 volume' % clusters)
        return reserved, fat_sectors, root_sectors, data_start, clusters
    sys.exit('could not lay out the volume')


def short_name(name, used):
    """8.3 alias, plain ASCII only (anything else lives in the long name)."""
    base, ext = os.path.splitext(name)
    ok = lambda s: ''.join(c for c in s.upper() if (c.isascii() and c.isalnum()) or c in '_-')
    b, e = ok(base) or 'GIF', ok(ext[1:])[:3]
    if len(b) <= 8 and b == base and e == ext[1:] and (b, e) not in used:
        cand = (b, e)
    else:
        n = 1
        while (b[:7 - len(str(n))] + '~' + str(n), e) in used:
            n += 1
        cand = (b[:7 - len(str(n))] + '~' + str(n), e)
    used.add(cand)
    return cand[0].ljust(8).encode('ascii') + cand[1].ljust(3).encode('ascii')


def needs_lfn(name, sfn):
    base, ext = sfn[:8].decode().rstrip(), sfn[8:].decode().rstrip()
    return name != (base + '.' + ext if ext else base)


def lfn_entries(name, sfn):
    csum = 0
    for c in sfn:
        csum = (((csum & 1) << 7) + (csum >> 1) + c) & 0xFF
    raw = name.encode('utf-16-le')  # UTF-16 code units, surrogate pairs included
    units = list(struct.unpack('<%dH' % (len(raw) // 2), raw))
    if len(units) > 255:
        sys.exit('file name too long: %s' % name)
    if len(units) % 13:  # NUL terminator + 0xFFFF padding only when the last entry is not full
        units += [0]
        units += [0xFFFF] * (-len(units) % 13)
    chunks = [units[i:i + 13] for i in range(0, len(units), 13)]
    out = []
    for seq in range(len(chunks), 0, -1):
        ch = chunks[seq - 1]
        e = bytearray(32)
        e[0] = seq | (0x40 if seq == len(chunks) else 0)
        e[1:11] = struct.pack('<5H', *ch[0:5])
        e[11], e[12], e[13] = 0x0F, 0, csum
        e[14:26] = struct.pack('<6H', *ch[5:11])
        e[28:32] = struct.pack('<2H', *ch[11:13])
        out.append(bytes(e))
    return out


def build_volume(files, total_sectors, label=b'SC26GIFS   '):
    reserved, fat_sectors, root_sectors, data_start, clusters = layout(total_sectors)
    img = bytearray(data_start * SECTOR)  # metadata only; file data is placed cluster by cluster
    data = {}                             # cluster number -> bytes

    bs = bytearray(SECTOR)
    bs[0:3] = b'\xEB\x3C\x90'
    bs[3:11] = b'MSDOS5.0'
    struct.pack_into('<HBHBHHBHHHII', bs, 11, SECTOR, SECTORS_PER_CLUSTER, reserved, 2, ROOT_ENTRIES,
                     total_sectors if total_sectors < 65536 else 0, 0xF8, fat_sectors, 63, 255, 0,
                     total_sectors if total_sectors >= 65536 else 0)
    struct.pack_into('<BBBI', bs, 36, 0x80, 0, 0x29, 0x5C262026)
    bs[43:54] = label
    bs[54:62] = b'FAT16   '
    bs[510:512] = b'\x55\xAA'
    img[0:SECTOR] = bs

    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0xFFF8, 0xFFFF
    entries = [label + bytes([0x08]) + bytes(20)]
    used = set()
    next_cluster = 2
    for name, payload in files:
        sfn = short_name(name, used)
        n_cl = max(1, -(-len(payload) // (SECTORS_PER_CLUSTER * SECTOR)))
        if next_cluster + n_cl > clusters + 2:
            sys.exit('not enough room for %s' % name)
        first = next_cluster
        for i in range(n_cl):
            c = first + i
            fat[c] = c + 1 if i < n_cl - 1 else 0xFFFF
            data[c] = payload[i * SECTORS_PER_CLUSTER * SECTOR:(i + 1) * SECTORS_PER_CLUSTER * SECTOR]
        next_cluster += n_cl
        entries += lfn_entries(name, sfn) if needs_lfn(name, sfn) else []
        d = bytearray(32)
        d[0:11] = sfn
        d[11] = 0x20  # archive
        date = ((2026 - 1980) << 9) | (10 << 5) | 27
        struct.pack_into('<HHHHHHHI', d, 14, 0, date, date, 0, 0, date, first, len(payload))
        entries.append(bytes(d))
    if len(entries) > ROOT_ENTRIES:
        sys.exit('too many files for the root directory')

    fat_bytes = struct.pack('<%dH' % len(fat), *fat).ljust(fat_sectors * SECTOR, b'\0')
    for i in range(2):
        off = (reserved + i * fat_sectors) * SECTOR
        img[off:off + len(fat_bytes)] = fat_bytes
    root_off = (reserved + 2 * fat_sectors) * SECTOR
    root = b''.join(entries)
    img[root_off:root_off + len(root)] = root
    return img, data, data_start, next_cluster - 2


def to_uf2(chunks, base):
    """chunks: dict flash_offset -> 256-byte payload."""
    addrs = sorted(chunks)
    out = bytearray()
    for i, off in enumerate(addrs):
        blk = struct.pack('<IIIIIIII', 0x0A324655, 0x9E5D5157, 0x00002000, base + off, UF2_PAYLOAD,
                          i, len(addrs), UF2_FAMILY_ABSOLUTE)
        blk += chunks[off].ljust(476, b'\0') + struct.pack('<I', 0x0AB16F30)
        out += blk
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('gifs', nargs='+')
    ap.add_argument('--offset', default='0x100000', help='flash offset of the GIF drive (default 0x100000)')
    ap.add_argument('--flash-size', default='16M')
    ap.add_argument('--no-readme', action='store_true')
    a = ap.parse_args()

    offset, flash = int(a.offset, 0), size_arg(a.flash_size)
    if offset % FLASH_ERASE or offset < MIN_DRIVE_OFFSET or offset >= flash:
        sys.exit('--offset must be 4 KiB aligned, at least 0x%x (the firmware slot) and inside the flash'
                 % MIN_DRIVE_OFFSET)
    total_sectors = (flash - offset) // SECTOR
    files = []
    for path in a.gifs:
        with open(path, 'rb') as f:
            payload = f.read()
        if payload[:6] not in (b'GIF87a', b'GIF89a'):
            sys.exit('%s is not a GIF' % path)
        files.append((os.path.basename(path), payload))
    if not a.no_readme:
        files.append(('README.TXT', README))
    seen = set()
    for name, _ in files:
        if any(c in '"*/:<>?\\|' or ord(c) < 32 for c in name):
            sys.exit('%s: FAT file names cannot contain " * / : < > ? \\ | or control characters' % name)
        if name.casefold() in seen:
            sys.exit('two files are called %s (FAT names ignore case)' % name)
        seen.add(name.casefold())

    meta, data, data_start, used = build_volume(files, total_sectors)
    # Whole 4 KiB erase sectors only: the boot ROM erases a sector before programming it,
    # so every page of a touched sector must be supplied.
    vol = {}
    for i in range(0, len(meta), UF2_PAYLOAD):
        vol[i] = bytes(meta[i:i + UF2_PAYLOAD])
    cl_bytes = SECTORS_PER_CLUSTER * SECTOR
    for c, payload in data.items():
        start = data_start * SECTOR + (c - 2) * cl_bytes
        padded = payload.ljust(cl_bytes, b'\0')
        for i in range(0, cl_bytes, UF2_PAYLOAD):
            vol[start + i] = padded[i:i + UF2_PAYLOAD]
    for off in list(vol):
        sec = off - off % FLASH_ERASE
        for p in range(sec, sec + FLASH_ERASE, UF2_PAYLOAD):
            vol.setdefault(p, bytes(UF2_PAYLOAD))

    blocks = {offset + k: v for k, v in vol.items()}
    # The firmware's power-loss journal sits just below the drive; erase it so it cannot
    # replay a metadata block from the volume this image replaces.
    for p in range(offset - JOURNAL_BYTES, offset, UF2_PAYLOAD):
        blocks[p] = b'\xff' * UF2_PAYLOAD
    uf2 = to_uf2(blocks, XIP_BASE)
    with open(a.out, 'wb') as f:
        f.write(uf2)
    print('%s: %d file(s), %d clusters used, %d UF2 blocks (%d KB) at flash 0x%08x' % (
        a.out, len(files), used, len(uf2) // 512, len(uf2) // 1024, XIP_BASE + offset))
    print('NOTE: only drop this on an SC26 badge running the GIF firmware. The RP2350 boot ROM accepts\n'
          '      absolute-address UF2s on ANY RP2350, and on another board this overwrites flash from\n'
          '      0x%08x on (e.g. the tail of a large firmware image).' % (XIP_BASE + offset - JOURNAL_BYTES))


if __name__ == '__main__':
    main()
