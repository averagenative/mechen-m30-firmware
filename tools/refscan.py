"""List stock function entries (SAVE prologues) inside patched ranges and every reference to them."""
import struct, sys
d = open(sys.argv[1], 'rb').read()
hw = lambda o: struct.unpack_from('<H', d, o)[0]
# (file_start, vaddr_start, length)
ranges = [(int(a, 0), int(b, 0), int(c, 0)) for a, b, c in (x.split(':') for x in sys.argv[2:])]
jals = {}
for o in range(0, len(d) - 4, 2):
    h1 = hw(o)
    if h1 & 0xfc00 == 0x1800:                       # MIPS16 jal (not jalx)
        t = (((h1 & 0x1f) << 21) | (((h1 >> 5) & 0x1f) << 16) | hw(o + 2)) << 2
        jals.setdefault(t & 0x0fffffff, []).append(o)
words = {}
for o in range(0, len(d) - 4, 2):
    words.setdefault(struct.unpack_from('<I', d, o)[0], []).append(o)
for fo, va, n in ranges:
    print('range %#x..%#x (file %#x)' % (va, va + n, fo))
    for o in range(fo, fo + n, 2):
        h = hw(o)
        ext = (hw(o - 2) & 0xf800) == 0xf000
        if h & 0xff80 == 0x6480 and not ext or (h & 0xff80 == 0x6480 and ext):
            e = va + (o - fo) - (2 if ext else 0)
            rj = [hex(x) for x in jals.get(e & 0x0fffffff, []) if not fo <= x < fo + n]
            rp = [hex(x) for x in words.get(e | 1, []) + words.get(e, [])]
            print('  entry %#x  jal-from %s  ptr-at %s' % (e, rj[:8], rp[:8]))
