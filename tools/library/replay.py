#!/usr/bin/env python3
"""Replay the library passes' file operations on the player, with self-describing data.

  replay.py gen   harness_new.txt IOTEST.OPS [--prefill]   an op stream from a -DTRACE_OPS harness
                                                 run (--prefill: first write what it only reads)
  replay.py check IOTEST.OPS MUSIC.LIB MUSIC.TMP  what the player (or the QEMU harness) left behind
  replay.py synth IOTEST.OPS [seed]              a synthetic stream of sector-aligned operations

The op stream is what replay.c runs on the player: every seek, read and write the passes made,
in order, on the same two files.  Each write fills its bytes with a pattern that names the file
and the byte offset (the 32-bit word at an aligned offset w of file f is w ^ KEY[f]), and every
read whose bytes were all written before is checked against that pattern.  "check" compares
the two files with the same model, sector by sector, and decodes what a wrong sector holds.

File layout (little-endian): "OPS3", u32 op count, u32 MUSIC.TMP size, u32 sum of the u16
words of the body, then 6 bytes per op:
  u16 = code | handle << 2 | flag << 3 | sectors << 4   (code 0 seek, 1 read, 2 write; handle
        0 MUSIC.LIB, 1 MUSIC.TMP; flag: check this read; length in 512-byte sectors)
  u32 = the seek target (seeks) or the position the op starts at (reads and writes, for the check)
"""
import re
import struct
import sys

KEY = {1: 0x4c490000, 2: 0x544d0000}
SECTOR = 512


def parse_trace(path):
    ops = []
    for line in open(path):
        if line.startswith('fs op 7'):
            break                            # the close in teardown; the stock's own ops follow
        m = re.match(r'fs op (\d+) h(\d+) at (\d+) a=0x([0-9a-f]+) b=(\d+)', line)
        if not m:
            continue
        op, h, at, a, b = int(m[1]), int(m[2]), int(m[3]), int(m[4], 16), int(m[5])
        if op == 0xb:
            ops.append((op, h, a, 0))
        elif op in (0xd, 0xe):
            ops.append((op, h, at, b))
    return ops


def simulate(ops):
    """Positions, the written-byte masks and the per-read check flags."""
    pos = {1: 0, 2: 0}
    size = {1: 0, 2: 0}
    for op, h, p, n in ops:
        if op != 0xb:
            size[h] = max(size[h], p + n)
    written = {h: bytearray(size[h]) for h in size}
    out = []
    for op, h, p, n in ops:
        flag = 0
        if op == 0xb:
            pos[h] = p
        else:
            assert pos[h] == p, (op, h, p, n, pos[h])
            if op == 0xe:
                written[h][p:p + n] = b'\1' * n
            elif all(written[h][p:p + n]):
                flag = 1
            pos[h] += n
        out.append((op, h, p, n, flag))
    return out, written


def pattern(h, start, n):
    """The bytes the player writes at start..start+n of file h."""
    k = KEY[h]
    w0 = start & ~3
    words = b''.join(struct.pack('<I', w ^ k) for w in range(w0, start + n, 4))
    return words[start - w0:start - w0 + n]


def gen(trace, out, prefill=False):
    ops = parse_trace(trace)
    if prefill:
        # write the part of MUSIC.LIB the stream only reads (the records) first, in 4 KB-bounded
        # sector pieces from 0x200, so those reads are checked too
        hi = max((p + n for op, h, p, n in ops if op == 0xd and h == 1), default=0)
        pre = [(0xb, 1, 0x200, 0)]
        p = 0x200
        while p < hi:
            n = min(4096 - p % 4096, hi - p)
            pre.append((0xe, 1, p, n)); p += n
        ops = pre + ops
    ops, written = simulate(ops)
    tmp_size = (len(written[2]) + 0xffff) & ~0xffff
    save(out, ops, tmp_size)
    nr = sum(1 for o in ops if o[0] == 0xd)
    nc = sum(1 for o in ops if o[4])
    print(f'{len(ops)} ops, {nr} reads of which {nc} checked, MUSIC.TMP size {tmp_size:#x}, '
          f'MUSIC.LIB written up to {len(written[1]):#x}')


def synth(out, seed=1):
    """Sector-aligned reads and writes only, on both files, interleaved: sequential fills,
    random seeks with random lengths up to 8 KB (many crossing 32 KB cluster boundaries in
    the middle of a transfer), sequential follow-ons without a seek, then everything read back."""
    import random
    rnd = random.Random(seed)
    TMP_END, LIB_LO, LIB_END, BLK = 0x420000, 0x200, 0x460200, 8192
    lim = {1: LIB_END, 2: TMP_END}
    ops = []
    pos = {1: None, 2: None}

    def seek(h, p):
        ops.append((0xb, h, p, 0)); pos[h] = p

    def io(op, h, n):
        ops.append((op, h, pos[h], n)); pos[h] += n

    seek(2, 0)
    while pos[2] < TMP_END: io(0xe, 2, min(BLK, TMP_END - pos[2]))
    seek(1, LIB_LO)
    while pos[1] < LIB_END: io(0xe, 1, min(BLK, LIB_END - pos[1]))
    for rounds in range(2):
        for i in range(700):
            h = rnd.choice((1, 2))
            n = rnd.randrange(1, 17) * 512
            lo = LIB_LO if h == 1 else 0
            p = lo + rnd.randrange((lim[h] - lo - n) // 512) * 512
            seek(h, p); io(0xd if rounds == 0 or rnd.random() < 0.6 else 0xe, h, n)
            while rnd.random() < 0.4 and pos[h] + BLK <= lim[h]:
                io(0xd if rnd.random() < 0.5 else 0xe, h, rnd.randrange(1, 17) * 512)
        for h in (2, 1):
            seek(h, LIB_LO if h == 1 else 0)
            while pos[h] < lim[h]: io(0xd, h, min(BLK, lim[h] - pos[h]))
    sim, written = simulate(ops)
    save(out, sim, TMP_END)
    nr = sum(1 for o in sim if o[0] == 0xd)
    nc = sum(1 for o in sim if o[4])
    cross = sum(1 for o in sim if o[0] != 0xb and o[2] // 0x8000 != (o[2] + o[3] - 1) // 0x8000)
    print(f'{len(sim)} ops, {nr} reads of which {nc} checked, {cross} transfers cross a 32 KB boundary, '
          f'{sum(o[3] for o in sim) / 2**20:.1f} MB moved')


CODE = {0xb: 0, 0xd: 1, 0xe: 2}


def save(out, ops, tmp_size):
    body = b''
    for op, h, p, n, flag in ops:
        assert n % 512 == 0 and n < 512 * 4096, (op, h, p, n)
        body += struct.pack('<HI', CODE[op] | (h - 1) << 2 | flag << 3 | (n // 512) << 4, p)
    total = sum(struct.unpack_from('<H', body, i)[0] for i in range(0, len(body), 2)) & 0xffffffff
    with open(out, 'wb') as f:
        f.write(b'OPS3' + struct.pack('<III', len(ops), tmp_size, total) + body)


def load_ops(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'OPS3'
    nops, tmp_size = struct.unpack_from('<II', d, 4)
    ops = []
    back = {v: k for k, v in CODE.items()}
    for i in range(nops):
        w0, p = struct.unpack_from('<HI', d, 16 + 6 * i)
        ops.append((back[w0 & 3], (w0 >> 2 & 1) + 1, p, (w0 >> 4) * 512, w0 >> 3 & 1))
    return ops, tmp_size


def ranges(flags):
    """Run-length encode a list of per-sector classes."""
    out = []
    for i, c in enumerate(flags):
        if out and out[-1][2] == c:
            out[-1][1] = i
        else:
            out.append([i, i, c])
    return out


def decode(h, s, data):
    """What a wrong sector holds."""
    if not any(data):
        return 'Z'
    w = struct.unpack_from('<I', data, 0)[0]
    for f, k in KEY.items():
        off = w ^ k
        if off % 4 == 0 and off < 0x2000000:
            # the whole sector must agree
            if data == pattern(f, off, SECTOR):
                return f'P{f}@{off:#x}'
    return '?'


def check(opsfile, lib, tmp):
    ops, tmp_size = load_ops(opsfile)
    written = {1: bytearray(), 2: bytearray()}
    for op, h, p, n, flag in ops:
        if op == 0xe:
            if len(written[h]) < p + n:
                written[h].extend(b'\0' * (p + n - len(written[h])))
            written[h][p:p + n] = b'\1' * n
    files = {1: open(lib, 'rb').read(), 2: open(tmp, 'rb').read()}
    names = {1: 'MUSIC.LIB', 2: 'MUSIC.TMP'}
    for h in (1, 2):
        data, wr = files[h], written[h]
        nsec = (len(wr) + SECTOR - 1) // SECTOR
        classes = []
        bad = 0
        for s in range(nsec):
            lo, hi = s * SECTOR, min((s + 1) * SECTOR, len(wr))
            if h == 1 and s < 2:
                classes.append('-')          # header and the result text
                continue
            if not any(wr[lo:hi]):
                classes.append('.')          # never written
                continue
            exp = pattern(h, lo, hi - lo)
            got = data[lo:hi] if lo < len(data) else b''
            ok = all(got[i] == exp[i] for i in range(hi - lo) if wr[lo + i]) if len(got) == hi - lo else False
            if ok:
                classes.append('E')
            else:
                bad += 1
                classes.append(decode(h, s, got) if len(got) == SECTOR else '!')
        print(f'{names[h]}: {nsec} sectors touched, {bad} wrong')
        line = []
        for lo, hi, c in ranges(classes):
            if c in '.-':
                continue
            line.append(f'{lo}-{hi} {c}' if hi != lo else f'{lo} {c}')
        # compress: print at most 60 ranges
        print('  ' + '  '.join(line[:60]) + ('  ...' if len(line) > 60 else ''))
    # the player's per-op result map, if it is there
    if len(files[1]) > 0xa00000 + len(ops) // 8:
        m = files[1][0xa00000:0xa00000 + (len(ops) + 7) // 8]
        flagged = [i for i in range(len(ops)) if m[i >> 3] >> (i & 7) & 1]
        if flagged:
            print(f'per-op map: {len(flagged)} ops flagged; first:')
            for i in flagged[:12]:
                op, h, p, n, flag = ops[i]
                print(f'  op {i}: {"seek" if op == 0xb else "read" if op == 0xd else "write"} h{h} '
                      f'pos {p:#x} len {n}')
        else:
            print('per-op map: nothing flagged')
    print(files[1][0x200:0x400].split(b'\0')[0].decode('latin1'))


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        gen(sys.argv[2], sys.argv[3], '--prefill' in sys.argv[4:])
    elif sys.argv[1] == 'check':
        check(sys.argv[2], sys.argv[3], sys.argv[4])
    elif sys.argv[1] == 'synth':
        synth(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    else:
        sys.exit(__doc__)
