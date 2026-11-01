"""Resolve (*DAT_xxx)(...,ID) dispatcher calls in Ghidra C output against an .ap file."""
import re, struct, sys, collections
sys.path.insert(0, sys.argv[3])
from apinfo import parse
d, hdr, banks = parse(sys.argv[1])
def rd(addr):
    for fo, a, s in banks:
        if a <= addr < a + s and fo:
            return struct.unpack_from('<I', d, fo + addr - a)[0]
c = open(sys.argv[2]).read()
calls = collections.Counter()
for m in re.finditer(r'\(\*DAT_([0-9a-f]{8})\)\(([^;]*)\);', c):
    tgt = rd(int(m.group(1), 16))
    args = m.group(2).rsplit(',', 1)
    fid = args[-1].strip() if len(args) > 1 else '?'
    calls[(tgt, fid)] += 1
by = collections.defaultdict(list)
for (t, f), n in calls.items(): by[t].append((f, n))
for t, l in sorted(by.items(), key=lambda x: -sum(n for _, n in x[1])):
    print('%s: %s' % (hex(t) if t else None, ' '.join('%s(x%d)' % (f, n) for f, n in sorted(l))))
