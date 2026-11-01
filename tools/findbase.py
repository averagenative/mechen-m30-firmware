"""Guess load bases of file regions from MIPS16e JAL targets vs SAVE prologues."""
import struct, sys, collections
d = open(sys.argv[1], 'rb').read()
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
hw = lambda o: struct.unpack_from('<H', d, o)[0]
prolog = set()
for o in range(0, len(d) - 2, 2):
    if hw(o) & 0xff80 == 0x6480:
        prolog.add(o - 2 if o >= 2 and hw(o - 2) & 0xf800 == 0xf000 else o)
targets = []
for o in range(lo, hi - 4, 2):
    h1 = hw(o)
    if h1 & 0xf800 == 0x1800 and not (h1 & 0x0400):  # JAL (x=0), not JALX
        t = (((h1 & 0x1f) << 21) | (((h1 >> 5) & 0x1f) << 16) | hw(o + 2)) << 2
        targets.append((o, t))
diff = collections.Counter()
for o, t in targets:
    for p in prolog:
        diff[(t - p) & 0x0fffffff] += 1
print('jal count', len(targets))
for base, n in diff.most_common(8):
    hits = sorted({p for o, t in targets for p in prolog if (t - p) & 0x0fffffff == base})
    print('base %#09x  hits %3d  prologue file range %#x-%#x' % (base, n, hits[0], hits[-1]))
