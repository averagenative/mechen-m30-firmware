"""Compare two MUSIC.LIB files section by section.

usage: libdiff.py EXPECTED.LIB GOT.LIB
Reports the header, then for each section the node tables and the three per-song tables,
naming the first differing entry (album, artist, genre, or song title).
"""
import struct
import sys

SECTIONS = (('ALLSONG', 0x10), ('ALBUM', 0x2c), ('ARTIST', 0x48), ('GENRE', 0x64))
FIELDS = {'title': (0x00, 0x40), 'artist': (0x40, 0x18), 'album': (0x58, 0x30), 'genre': (0x88, 0x18)}


def text(b):
    if b[:2] == b'\xff\xfe':
        return b[2:].decode('utf-16-le', 'replace').split('\0')[0]
    return b.split(b'\0')[0].decode('latin-1')


class Lib:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.n = struct.unpack_from('<H', self.d, 8)[0]

    def rec(self, i):
        return self.d[0x200 + i * 256:0x200 + (i + 1) * 256]

    def field(self, i, name):
        o, w = FIELDS[name]
        return text(self.rec(i)[o:o + w])

    def section(self, off):
        counts = struct.unpack_from('<3H', self.d, off + 8)
        sectors = struct.unpack_from('<7H', self.d, off + 14)
        return counts, sectors

    def u16s(self, sector, count):
        return struct.unpack_from('<%dH' % count, self.d, sector * 512)

    def nodes(self, sector, count):
        return [self.d[sector * 512 + i * 16:sector * 512 + (i + 1) * 16] for i in range(count)]


def main(a, b):
    A, B = Lib(a), Lib(b)
    print('songs: expected %d, got %d' % (A.n, B.n))
    if A.d[:0x200] != B.d[:0x200]:
        diff = [i for i in range(0x200) if A.d[i] != B.d[i]]
        print('header differs at offsets', ' '.join('%#x' % i for i in diff[:16]))
    else:
        print('header identical')
    for name, off in SECTIONS:
        counts, sectors = A.section(off)
        bcounts, bsectors = B.section(off)
        print('%s: counts %s sectors %s' % (name, counts, sectors[:6]))
        if counts != bcounts or sectors != bsectors:
            print('   got counts %s sectors %s' % (bcounts, bsectors[:6]))
        field = {'ALLSONG': 'title', 'ALBUM': 'album', 'ARTIST': 'artist', 'GENRE': 'genre'}[name]
        for lvl in range(3):
            if not counts[lvl]:
                continue
            na, nb = A.nodes(sectors[lvl], counts[lvl]), B.nodes(bsectors[lvl], bcounts[lvl])
            same = sum(1 for x, y in zip(na, nb) if x == y)
            print('   level %d nodes: %d of %d identical' % (lvl, same, counts[lvl]))
            for k, (x, y) in enumerate(zip(na, nb)):
                if x != y:
                    fx, fy = struct.unpack_from('<H', x, 6)[0], struct.unpack_from('<H', y, 6)[0]
                    print('      first diff at node %d: expected %s (%d songs, first %d %r), got %s (%d songs, first %d %r)' % (
                        k, x.hex(), struct.unpack_from('<H', x, 4)[0], fx, A.field(fx, field),
                        y.hex(), struct.unpack_from('<H', y, 4)[0], fy, B.field(fy, field)))
                    break
        for slot, what in ((3, 'order'), (4, 'inverse'), (5, 'node index')):
            if not sectors[slot]:
                continue
            ta, tb = A.u16s(sectors[slot], A.n), B.u16s(bsectors[slot], B.n)
            same = sum(1 for x, y in zip(ta, tb) if x == y)
            print('   %s table: %d of %d identical' % (what, same, A.n))
            if same != A.n:
                k = next(i for i in range(A.n) if ta[i] != tb[i])
                if what == 'order':
                    print('      first diff at position %d: expected song %d %r / %r, got song %d %r / %r' % (
                        k, ta[k], A.field(ta[k], field), A.field(ta[k], 'title'),
                        tb[k], B.field(tb[k], field), B.field(tb[k], 'title')))
                else:
                    print('      first diff at song %d %r: expected %d, got %d' % (k, A.field(k, field), ta[k], tb[k]))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
