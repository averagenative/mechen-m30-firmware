"""Find MIPS16e EXTENDed immediates and 32-bit literals equal to given values in firmware files."""
import os, struct, sys
vals = {int(v, 0) for v in sys.argv[2].split(',')}
OPS = {0x08: 'addiu(rri)', 0x09: 'addiu8', 0x0a: 'slti', 0x0b: 'sltiu', 0x0c: 'i8', 0x0d: 'li', 0x0e: 'cmpi', 0x00: 'addiusp', 0x01: 'addiupc'}
for n in sorted(os.listdir(sys.argv[1])):
    d = open(os.path.join(sys.argv[1], n), 'rb').read()
    hits = []
    for o in range(0, len(d) - 4, 2):
        e = struct.unpack_from('<H', d, o)[0]
        if e & 0xf800 != 0xf000:
            continue
        i = struct.unpack_from('<H', d, o + 2)[0]
        op = i >> 11
        if op not in OPS:
            continue
        imm = ((e & 0x1f) << 11) | (((e >> 5) & 0x3f) << 5) | (i & 0x1f)
        if imm in vals:
            hits.append('%s@%#x' % (OPS[op], o))
    for o in range(0, len(d) - 3, 4):
        if struct.unpack_from('<I', d, o)[0] in vals:
            hits.append('word@%#x' % o)
    if hits:
        print('%-14s %s' % (n, ' '.join(hits[:12]) + (' ...(%d)' % len(hits) if len(hits) > 12 else '')))
