"""Recover untabled bank placement: vote (bank_no, file_start) from JAL targets
whose low 18 bits are window+offset and that land on SAVE prologues."""
import struct, sys, collections
d = open(sys.argv[1], 'rb').read()
window = int(sys.argv[2], 16)          # e.g. 0x28000
lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
hw = lambda o: struct.unpack_from('<H', d, o)[0]
prolog = []
for o in range(lo, hi - 2, 2):
    if hw(o) & 0xff80 == 0x6480:
        prolog.append(o - 2 if hw(o - 2) & 0xf800 == 0xf000 else o)
targets = set()
for o in range(0x600, len(d) - 4, 2):
    h1 = hw(o)
    if h1 & 0xf800 == 0x1800:
        t = (((h1 & 0x1f) << 21) | (((h1 >> 5) & 0x1f) << 16) | hw(o + 2)) << 2
        if (t & 0x3ffff) >= window and (t & 0x3ffff) < window + 0x800:
            targets.add(t)
votes = collections.Counter()
for t in targets:
    off = (t & 0x3ffff) - window
    for p in prolog:
        s = p - off
        if s >= lo and s % 0x100 == 0:
            votes[((t >> 18) & 0x3f, s, (t & 0x0fc00000) >> 24)] += 1
best = {}
for (n, s, grp), v in votes.most_common():
    if v >= 2 and n not in best and s not in [b[0] for b in best.values()]:
        best[n] = (s, v, grp)
for n in sorted(best, key=lambda n: best[n][0]):
    s, v, grp = best[n]
    print('bank %2d  file %#07x  votes %d  (28-bit group nibble %#x)' % (n, s, v, grp))
