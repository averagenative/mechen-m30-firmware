"""Make QEMU test copies of the playlist.ap banks that build the MUSIC.LIB index.

usage: testbanks.py playlist.ap OUTDIR
Writes OUTDIR/bank_<addr>.bin for each bank with every literal-pool pointer to player RAM
(0x9fc00000..) moved to the harness RAM at 0x10000000.., and every pointer to a resident
function or syscall stub moved to the harness stub for it (see harness.c and test.ld).
Pointers between banks stay: the harness links the banks at their real addresses.
"""
import struct
import sys

BANKS = {0x400d4c00: 0x3400, 0x40114c00: 0x3c00, 0x40194c00: 0x4c00, 0x40294c00: 0x6c00,
         0x48014400: 0x7400, 0x71812500: 0xa400}
RAM_PLAYER, RAM_TEST = 0x9fc00000, 0x10000000
STUB_LIBC, STUB_FS, STUB_APPEND, STUB_NOP, STUB_TEXT = 0x00700000, 0x00700400, 0x00700800, 0x00700c00, 0x00701000
RESIDENT = {0xbfc16409: STUB_LIBC,    # module 0x10: copy, fill, length
            0xbfc1640d: STUB_FS,      # module 0x11: file system
            0xbfc16411: STUB_APPEND,  # resident: append whole sectors, advance the sector counter
            0xbfc165e5: STUB_NOP,     # resident: message pump
            0xbfc16bb1: STUB_NOP,     # module 0x0f: kernel message
            0xbfc16bb5: STUB_TEXT,    # module 6: text conversion
            0x401d4c01: STUB_NOP,     # progress bar (bank 0x401d4c00, not loaded)
            0x401d4cb1: STUB_NOP}


def redirect(bank, base):
    b = bytearray(bank)
    moved = []
    for o in range(0, len(b), 4):
        v = struct.unpack_from('<I', b, o)[0]
        if RAM_PLAYER <= v < RAM_PLAYER + 0x100000:
            new = v - RAM_PLAYER + RAM_TEST
        elif v in RESIDENT:
            new = RESIDENT[v]
        elif 0xbfc10000 <= v < 0xbfc17000 or 0x401d4c00 <= v < 0x401d5400:
            raise SystemExit('bank %08x: unknown resident pointer %08x at +%#x' % (base, v, o))
        else:
            continue
        struct.pack_into('<I', b, o, new)
        moved.append((o, v, new))
    return b, moved


def main(app, out):
    d = open(app, 'rb').read()
    for base, fo in sorted(BANKS.items()):
        b, moved = redirect(d[fo:fo + 0x800], base)
        open('%s/bank_%08x.bin' % (out, base), 'wb').write(b)
        print('bank %08x: %d pointers moved' % (base, len(moved)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
