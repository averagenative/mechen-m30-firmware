"""Make a synthetic MUSIC.LIB with N scanned records, as the stock scan leaves it, for the
index-pass tests.

usage: mklib.py N OUT.LIB [SEED]
The names are pseudo-random but deterministic.  They include the cases the passes must get
right: titles shared between albums, albums shared between artists, names that differ only in
case, the same name stored as ASCII in one record and UTF-16 in another, tracks without a
number, and a few names with characters above U+00FF.
"""
import random
import struct
import sys

WORDS = ('Black', 'River', 'Night', 'Golden', 'Silver', 'Dream', 'Fire', 'Stone', 'Wild', 'Blue',
         'Heart', 'Road', 'Moon', 'Sun', 'Rain', 'Ghost', 'City', 'Light', 'Dark', 'Love',
         'Time', 'Wind', 'Sea', 'Sky', 'Star', 'Iron', 'Glass', 'Paper', 'Velvet', 'Neon')
GENRES = ('Rock', 'Pop', 'Jazz', 'Blues', 'Metal', 'Hip-Hop', 'Electronic', 'Folk', 'Punk Rock',
          'Soul', 'Funk', 'Country', 'Classical', 'Ambient', 'Indie', 'Reggae', 'Soundtrack',
          'Alternative', 'Trip-Hop', 'Gospel', 'Latin', 'Techno', 'House', 'Grunge', 'Ska',
          'Industrial', 'Dub', 'Shoegaze', 'Post-Rock', 'Drone')


def field(text, width, ascii_ok, rnd):
    """A record text field: UTF-16LE with BOM, or plain ASCII now and then."""
    if ascii_ok and rnd.random() < 0.08 and all(ord(c) < 128 for c in text):
        b = text.encode('ascii')[:width - 1]
    else:
        b = b'\xff\xfe' + text.encode('utf-16-le')[:width - 4]
    return b.ljust(width, b'\0')


def main(n, out, seed=1):
    rnd = random.Random(seed)
    artists = ['%s %s' % (rnd.choice(WORDS), rnd.choice(WORDS)) for _ in range(max(2, n // 15))]
    artists += ['The ' + a for a in artists[:len(artists) // 6]]
    albums = ['%s %s' % (rnd.choice(WORDS), rnd.choice(('Sessions', 'Live', 'Greatest Hits', 'EP', 'II', 'Songs')))
              for _ in range(max(2, n // 8))]
    albums += [a.upper() for a in albums[:len(albums) // 10]]            # case variants
    albums += ['Déjà Vu', 'Ἀποκάλυψις', '“Quoted”', 'Año Nuevo']
    hdr = bytearray(0x200)
    hdr[0:5] = b'MUSIC'
    struct.pack_into('<HH', hdr, 8, n, 1)
    hdr[0x1fe:0x200] = b'\x55\xaa'
    for off, name in ((0x10, b'ALLSONG'), (0x2c, b'ALBUM'), (0x48, b'ARTIST'), (0x64, b'GENRE')):
        hdr[off:off + len(name)] = name
    recs = []
    for i in range(n):
        r = bytearray(256)
        trk = rnd.randint(1, 18) if rnd.random() < 0.9 else 0xffff
        title = '%02d %s %s' % (trk if trk != 0xffff else rnd.randint(1, 30), rnd.choice(WORDS), rnd.choice(WORDS))
        if rnd.random() < 0.05:
            title = title.lower()
        r[0x00:0x40] = field(title, 0x40, True, rnd)
        r[0x40:0x58] = field(rnd.choice(artists), 0x18, True, rnd)
        r[0x58:0x88] = field(rnd.choice(albums), 0x30, True, rnd)
        r[0x88:0xa0] = field(rnd.choice(GENRES), 0x18, True, rnd)
        r[0xa0:0xa3] = (b'%d' % trk if trk != 0xffff else b'')[:3].ljust(3, b'\0')
        r[0xa3] = 1
        r[0xa4:0xa8] = b'MP3\0' if rnd.random() < 0.95 else b'M4A\0'
        r[0xa8] = 1
        r[0xa9:0xf2] = bytes(rnd.getrandbits(8) for _ in range(0xf2 - 0xa9))
        struct.pack_into('<H', r, 0xf2, trk)
        r[0xf4:0xfc] = bytes(rnd.getrandbits(8) for _ in range(8))
        struct.pack_into('<HH', r, 0xfc, (i - 1) & 0xffff, i + 1)
        recs.append(bytes(r))
    body = b''.join(recs)
    body = body.ljust((len(body) + 511) // 512 * 512, b'\0')
    open(out, 'wb').write(bytes(hdr) + body)
    print('wrote %s: %d songs, %d artists, %d albums' % (out, n, len(artists), len(albums)))


if __name__ == '__main__':
    main(int(sys.argv[1]), sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1)
