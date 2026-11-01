import sys, struct, glob, os
consts = {0x41c64e6d: 'glibc/ANSI LCG mult 1103515245', 0x3039: 'ANSI LCG incr 12345',
          0x343fd: 'MSVC LCG mult 214013', 0x269ec3: 'MSVC LCG incr 2531011',
          0x10dcd: 'LCG mult 69069', 0x5851f42d: 'PCG/Knuth64 hi', 0x19660d: 'Numerical Recipes 1664525',
          0x3c6ef35f: 'NR incr 1013904223', 0x41a7: 'MINSTD 16807', 0xbc8f: 'MINSTD 48271'}
for p in sorted(glob.glob(os.path.join(sys.argv[1], '*'))):
    d = open(p, 'rb').read()
    for c, name in consts.items():
        if c > 0xffff:
            raw = struct.pack('<I', c)
            for off in [i for i in range(len(d)) if d.startswith(raw, i)][:3]:
                print(f'{os.path.basename(p)}: raw {c:#x} ({name}) @ {off:#x}')
            # MIPS32 lui/ori pair: lui rX, hi ; ori/addiu rX, rX, lo
            hi, lo = c >> 16, c & 0xffff
            for i in range(0, len(d) - 8, 2):
                w = struct.unpack_from('<I', d, i)[0]
                if (w >> 26) == 0x0f and (w & 0xffff) == hi:
                    for j in range(i + 4, min(i + 24, len(d) - 4), 2):
                        w2 = struct.unpack_from('<I', d, j)[0]
                        if (w2 >> 26) in (0x0d, 0x09) and (w2 & 0xffff) == lo:
                            print(f'{os.path.basename(p)}: lui/ori {c:#x} ({name}) @ {i:#x}')
                            break
