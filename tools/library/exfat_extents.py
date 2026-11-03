#!/usr/bin/env python3
"""Show how files in the root of an exFAT card are allocated, from the raw device.

  sudo python3 -I tools/library/exfat_extents.py /dev/sda [NAME ...]

For every root entry (or the names given): first cluster, data length, the NoFatChain flag
(set when the file is one contiguous run and the FAT holds no chain for it), and its extents
as the FAT chain gives them.  Reads only; the device needs root to open.
"""
import struct
import sys


def main(dev, names):
    f = open(dev, 'rb')
    bs = f.read(512)
    assert bs[3:11] == b'EXFAT   ', 'not an exFAT volume'
    fat_off, fat_len, heap_off, ncl, root = struct.unpack_from('<IIIII', bs, 0x50)
    ssh, csh = bs[0x6c], bs[0x6d]
    sec, clu = 1 << ssh, 1 << csh                          # bytes per sector, sectors per cluster
    cl_bytes = sec * clu
    print(f'sector {sec} B, cluster {cl_bytes} B ({clu} sectors), {ncl} clusters, FAT at sector {fat_off} '
          f'({fat_len} sectors), heap at sector {heap_off}, root dir cluster {root}')

    def fat(c):
        f.seek(fat_off * sec + c * 4)
        return struct.unpack('<I', f.read(4))[0]

    def chain(first, nofat, length):
        n = (length + cl_bytes - 1) // cl_bytes
        if nofat:
            return [(first, n)]
        ext, c, cnt = [], first, 0
        start = first
        while c < 0xfffffff7 and cnt < n:
            cnt += 1
            nx = fat(c)
            if nx != c + 1 or cnt == n:
                ext.append((start, c - start + 1))
                start = nx
            c = nx
        return ext

    # the root directory: walk its own chain (its flags are not in a stream entry: use the FAT)
    entries = b''
    c = root
    while c < 0xfffffff7 and len(entries) < 64 * cl_bytes:
        f.seek((heap_off + (c - 2) * clu) * sec)
        entries += f.read(cl_bytes)
        c = fat(c)
        if c == 0:            # no chain written: assume contiguous and stop after one cluster
            break
    i = 0
    while i + 32 <= len(entries):
        t = entries[i]
        if t == 0:
            break
        if t & 0x7f == 0x05:                                   # a file entry, in use or deleted
            inuse = bool(t & 0x80)
            sec_count = entries[i + 1]
            attr = struct.unpack_from('<H', entries, i + 4)[0]
            se = entries[i + 32:i + 64]
            flags, nlen = se[1], se[3]
            first, = struct.unpack_from('<I', se, 0x14)
            length, = struct.unpack_from('<Q', se, 0x18)
            name = ''
            for k in range(2, sec_count + 1):
                ne = entries[i + 32 * k:i + 32 * k + 32]
                if ne[0] & 0x7f == 0x41:
                    name += ne[2:32].decode('utf-16le')
            name = name.rstrip('\0')
            if not names or name in names:
                nofat = bool(flags & 2)
                ext = chain(first, nofat, length) if first else []
                print(f'{"" if inuse else "(deleted) "}{name}: attr {attr:#06x}, {length} bytes, first cluster {first}, '
                      f'NoFatChain={int(nofat)}, {len(ext)} extent(s):')
                for s, n in ext:
                    print(f'    cluster {s} x {n}  (file offset {sum(x[1] for x in ext[:ext.index((s, n))]) * cl_bytes:#x}, '
                          f'sector {heap_off + (s - 2) * clu})')
            i += 32 * (sec_count + 1)
        else:
            i += 32


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
