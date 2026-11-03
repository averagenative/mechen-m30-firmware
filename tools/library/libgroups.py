"""Compare the grouping of two MUSIC.LIB files: which groups of EXPECTED are merged or split in GOT.

usage: libgroups.py EXPECTED.LIB GOT.LIB
"""
import struct
import sys
from collections import defaultdict

SECTIONS = (('ALBUM', 0x2c, 'album'), ('ARTIST', 0x48, 'artist'), ('GENRE', 0x64, 'genre'))
FIELDS = {'title': (0x00, 0x40), 'artist': (0x40, 0x18), 'album': (0x58, 0x30), 'genre': (0x88, 0x18)}


def text(b):
    if b[:2] == b'\xff\xfe':
        return b[2:].decode('utf-16-le', 'replace').split('\0')[0]
    return b.split(b'\0')[0].decode('latin-1')


def groups(d, off, n):
    """Level-0 groups in order, from the order table and the level-0 node song counts."""
    counts = struct.unpack_from('<3H', d, off + 8)
    secs = struct.unpack_from('<7H', d, off + 14)
    order = struct.unpack_from('<%dH' % n, d, secs[3] * 512)
    out, p = [], 0
    for i in range(counts[0]):
        songs = struct.unpack_from('<H', d, secs[0] * 512 + i * 16 + 4)[0]
        out.append(order[p:p + songs])
        p += songs
    return out


def main(a, b):
    A, B = open(a, 'rb').read(), open(b, 'rb').read()
    n = struct.unpack_from('<H', A, 8)[0]
    for name, off, field in SECTIONS:
        fo, fw = FIELDS[field]
        ga, gb = groups(A, off, n), groups(B, off, n)
        ia = {s: k for k, g in enumerate(ga) for s in g}
        ib = {s: k for k, g in enumerate(gb) for s in g}
        print('%s: expected %d groups, got %d' % (name, len(ga), len(gb)))
        for k, g in enumerate(gb):
            src = sorted({ia[s] for s in g})
            if len(src) > 1:
                print('  merged into one: ' + ' | '.join('%r (%d)' % (text(A[0x200 + ga[j][0] * 256 + fo:0x200 + ga[j][0] * 256 + fo + fw]), len(ga[j])) for j in src))
        for k, g in enumerate(ga):
            dst = sorted({ib[s] for s in g})
            if len(dst) > 1:
                print('  split: %r into %d groups' % (text(A[0x200 + g[0] * 256 + fo:0x200 + g[0] * 256 + fo + fw]), len(dst)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
