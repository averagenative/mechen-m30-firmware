"""List calls through a given syscall stub (module dispatcher) in Ghidra C output, with context."""
import re, struct, sys, collections
sys.path.insert(0, sys.argv[5]); from apinfo import parse
d, hdr, banks = parse(sys.argv[1])
extra = []
if sys.argv[4] != '-':
    extra = [tuple(int(x, 16) for x in s.split(':')) for s in open(sys.argv[4]).read().strip().split(',')]
allb = [(fo, a, 0x800) for fo, a in extra] + list(banks)
def rd(addr):
    for fo, a, s in allb:
        if a <= addr < a + s and fo + addr - a + 4 <= len(d): return struct.unpack_from('<I', d, fo + addr - a)[0]
stub = int(sys.argv[3], 16)
c = open(sys.argv[2]).read()
ids = collections.defaultdict(list)
for m in re.finditer(r'(\w+ = )?\(\*DAT_([0-9a-f]{8})\)\(([^;]*)\);', c):
    if rd(int(m.group(2), 16)) != stub: continue
    args = m.group(3).rsplit(',', 1)
    fid = args[-1].strip()
    func = c.rfind('// ===== ', 0, m.start()); fname = c[func + 9:c.index(' @', func)]
    ctx = c[m.end():m.end() + 160].replace('\n', ' ')
    ids[fid].append((fname, (m.group(1) or '').strip(), args[0][:40], re.sub(r'\s+', ' ', ctx)[:110]))
for fid in sorted(ids, key=lambda x: (len(x), x)):
    v = ids[fid]
    print('fn %-8s x%-3d e.g. %s %s(%s) ...%s' % (fid, len(v), v[0][0], v[0][1], v[0][2], v[0][3]))
