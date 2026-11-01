"""Decode the Actions ATJ2127 MUSIC.LIB media library (as built by playlist.ap)."""
import struct, sys

def text(b):
    if b[:2] == b'\xff\xfe':
        return b[2:].decode('utf-16-le', 'replace').split('\0')[0]
    return b.split(b'\0')[0].decode('latin-1')

def records(d, n):
    for i in range(n):
        r = d[0x200 + i * 0x100:0x300 + i * 0x100]
        yield dict(i=i, title=text(r[0:0x40]), artist=text(r[0x40:0x58]), album=text(r[0x58:0x88]),
                   genre=text(r[0x88:0xa0]), trk=r[0xa0:0xa3], ok=r[0xa3], ext=r[0xa4:0xa8],
                   loc=r[0xa8:0xf2], f2=struct.unpack_from('<H', r, 0xf2)[0], f4=r[0xf4:0xfc],
                   prev=struct.unpack_from('<H', r, 0xfc)[0], next=struct.unpack_from('<H', r, 0xfe)[0])

def header(d):
    secs = []
    off = 0
    while off < 0x200:
        name = d[off:off + 8].split(b'\0')[0]
        if not name: break
        secs.append((off, name.decode()))
        off += 8
        # skip to next ASCII name
        while off < 0x200 and not (65 <= d[off] <= 90 and d[off + 1] != 0):
            off += 2
    return secs

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    print('header u16 by offset:')
    for o in range(0, 0x80, 2):
        w = struct.unpack_from('<H', d, o)[0]
        print('  %#04x %04x %s' % (o, w, repr(d[o:o + 2]) if 0x20 <= d[o] < 0x7f and 0x20 <= d[o+1] < 0x7f else ''))
