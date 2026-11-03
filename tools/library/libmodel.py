"""Reference model of the file-backed index passes (tools/library/index.c).

usage: libmodel.py INPUT.LIB EXPECTED.LIB
Reads the header and records of INPUT.LIB, computes the index tables and nodes the new
passes must produce, and writes EXPECTED.LIB with the same records.  Compare the output of
the passes against it with libdiff.py; every table must be identical.

The text conversion is the harness's: characters up to U+00FF become their byte, the first
character above that stops the conversion, and the rest of the buffer keeps the UTF-16 bytes
(the converter copies twice the field width from two bytes past the field start, so the
tail comes from the following record fields).
"""
import functools
import struct
import sys

SECTOR = 512
FIELDS = ((0x00, 0x40), (0x58, 0x30), (0x40, 0x18), (0x88, 0x18))    # title, album, artist, genre


def convert(rec, foff, W):
    f = rec[foff:foff + W]
    if f[:2] != b'\xff\xfe':
        return bytes(f)
    buf = bytearray(rec[foff + 2:foff + 2 + 2 * W].ljust(2 * W, b'\0'))
    for i in range(W):
        ch = buf[2 * i] | (buf[2 * i + 1] << 8)
        if ch >= 256:
            break
        buf[i] = ch
        if ch == 0:
            break
    return bytes(buf[:W])


def up(b):
    return b - 32 if 0x61 <= b <= 0x7a else b


def cmptext(x, y):
    for a, b in zip(x, y):
        a, b = up(a), up(b)
        if a != b:
            return -1 if a < b else 1
        if a == 0:
            return 0
    return 0


class Writer:
    def __init__(self, first_sector):
        self.sector = first_sector
        self.chunks = []

    def append(self, data):
        n = (len(data) + SECTOR - 1) // SECTOR
        self.chunks.append(bytes(data).ljust(n * SECTOR, b'\0'))
        sec = self.sector
        self.sector += n
        return sec


def u16s(vals):
    return struct.pack('<%dH' % len(vals), *vals)


def nodes_of(order, runs, child_runs, parent_runs, depth):
    out, pos, child_idx, parent_idx, parent_acc, parent_len = bytearray(), 0, 0, 0, 0, (parent_runs or [0])[0]
    p_i = 1
    c_i = 0
    for k, songs in enumerate(runs):
        first = order[pos]
        pos += songs
        if parent_runs is None:
            parent = 0xffff
        else:
            parent = parent_idx
            parent_acc += songs
            if parent_len <= parent_acc:
                parent_acc = 0
                parent_len = parent_runs[p_i] if p_i < len(parent_runs) else 0
                p_i += 1
                parent_idx += 1
        if child_runs is None:
            first_child, nch = 0xffff, 0
        else:
            first_child, nch, acc = child_idx, 0, 0
            while acc < songs:
                acc += child_runs[c_i]
                c_i += 1
                nch += 1
                child_idx += 1
        out += struct.pack('<BBHHHHHHH', 1, depth, nch, songs, first, first_child, parent,
                           0xffff if k == 0 else k - 1, 0xffff if k == len(runs) - 1 else k + 1)
    return bytes(out)


def main(inp, outp):
    d = bytearray(open(inp, 'rb').read())
    n = struct.unpack_from('<H', d, 8)[0]
    recs = [bytes(d[0x200 + i * 256:0x200 + (i + 1) * 256]) for i in range(n)]
    w = Writer(1 + (n * 256 + SECTOR - 1) // SECTOR)
    keys = [[convert(r, fo, W) for r in recs] for fo, W in FIELDS]
    trk = [struct.unpack_from('<b', r, 0xf2)[0] for r in recs]    # signed low byte of the track number
    rank = None            # inverse of the previous section's order
    album_group = None     # per song, from the ALBUM section
    artist_group = None    # per song, from the ARTIST section
    header = bytearray(d[:0x200])
    for sec in range(4):
        text = keys[sec]
        def cmp(a, b):
            r = cmptext(text[a], text[b])
            if r:
                return r
            if sec == 1 and trk[a] != trk[b]:
                return -1 if trk[a] < trk[b] else 1
            if sec and rank[a] != rank[b]:
                return -1 if rank[a] < rank[b] else 1
            return 0
        order = sorted(range(n), key=functools.cmp_to_key(cmp))
        levels = 0 if sec == 0 else sec
        runs = [[] for _ in range(levels)]
        if levels:
            runs = [[] for _ in range(levels)]
            lens = [0] * levels
            for pos in range(n):
                if pos:
                    a, b = order[pos - 1], order[pos]
                    brk = 3
                    if cmptext(text[a], text[b]) != 0:
                        brk = 0
                    elif levels > 1 and (album_group if sec == 2 else artist_group)[a] != (album_group if sec == 2 else artist_group)[b]:
                        brk = 1
                    elif levels > 2 and album_group[a] != album_group[b]:
                        brk = 2
                    for lvl in range(brk, levels):
                        runs[lvl].append(lens[lvl])
                        lens[lvl] = 0
                for lvl in range(levels):
                    lens[lvl] += 1
            for lvl in range(levels):
                runs[lvl].append(lens[lvl])
        hoff = (0x10, 0x2c, 0x48, 0x64)[sec]
        counts, nsecs = [0, 0, 0], [0, 0, 0]
        for lvl in range(levels):
            nsecs[lvl] = w.sector
            data = nodes_of(order, runs[lvl], runs[lvl + 1] if lvl + 1 < levels else None,
                            runs[lvl - 1] if lvl else None, levels - lvl)
            counts[lvl] = len(runs[lvl])
            if data:
                w.append(data)
        asec = w.append(u16s(order))
        inv = [0] * n
        for pos, song in enumerate(order):
            inv[song] = pos
        bsec = w.append(u16s(inv))
        fsec = 0
        if levels:
            grp = [0] * n
            pos = 0
            for g, ln in enumerate(runs[levels - 1]):
                for song in order[pos:pos + ln]:
                    grp[song] = g
                pos += ln
            fsec = w.append(u16s(grp))
            if sec == 1:
                album_group = grp
            if sec == 2:
                artist_group = [0] * n
                pos = 0
                for g, ln in enumerate(runs[0]):
                    for song in order[pos:pos + ln]:
                        artist_group[song] = g
                    pos += ln
        rank = inv
        struct.pack_into('<3H', header, hoff + 8, *counts)
        struct.pack_into('<7H', header, hoff + 14, nsecs[0], nsecs[1], nsecs[2], asec, bsec, fsec, 0)
    out = bytes(header) + bytes(d[0x200:(1 + (n * 256 + SECTOR - 1) // SECTOR) * SECTOR]) + b''.join(w.chunks)
    open(outp, 'wb').write(out)
    print('expected library: %d songs, %d sectors' % (n, w.sector))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
