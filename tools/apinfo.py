"""Parse an Actions ATJ2127 .ap overlay file: header + bank table at 0x100."""
import struct, sys

def parse(path):
    d = open(path, 'rb').read()
    assert d[4:8] == b'yqhx', 'not an AP file'
    hdr = struct.unpack_from('<12I', d, 0x2c)
    banks = []
    for off in range(0x100, 0x100 + hdr[0] - 11, 12):
        fo, addr, size = struct.unpack_from('<3I', d, off)
        if fo and addr and 0 < size <= 0x10000:
            banks.append((fo, addr, size))
    return d, hdr, banks

if __name__ == '__main__':
    for p in sys.argv[1:]:
        d, hdr, banks = parse(p)
        print(p.split('/')[-1], 'len', hex(len(d)))
        print('  hdr@0x2c:', ' '.join('%08x' % h for h in hdr[:8]))
        for fo, addr, size in banks:
            print('  bank file@%#07x -> %#010x size %#06x' % (fo, addr, size))
