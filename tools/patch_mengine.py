"""Patch mengine.ap (2025-06-26 firmware) with the shuffle fix and, optionally, whole-card shuffle.

usage: patch_mengine.py STOCK_mengine.ap OUT.ap SHUFFLE.bin [A.bin B.bin C.bin]

Both edits live in mengine's hidden bank group 0x7b (not in the .ap bank table):
  bank 1: file 0x7300 -> 0x7b068000      bank 6: file 0x8d00 -> 0x7b1a8000

Shuffle fix (tools/shuffle):
  SHUFFLE.bin over the stock init_shuffle at 0x7b1a808c (ends at 0x7b1a82ac), and the stock
  picker entry 0x7b1a84b0 becomes `b 0x7b1a817c` (the new shuffle_next), leaving the rest of
  the old picker body dead.
Whole-card shuffle (tools/playall):
  A (walker)       over the dead disk-wide counter 0x7b068484, must end before 0x7b06856c
                   (live folder-mode get_location, called through the file-selector table)
  B (folder perm)  in the free tail 0x7b1a82ac..0x7b1a83ec
  C (playall_next) in the dead picker body 0x7b1a84b4..0x7b1a8568
  hook: the Repeat All path `jal 0x7b0681e8` at 0x7b068088 becomes `jal 0x7b1a84b4`.
Every edit checks the stock bytes first, so a different firmware version fails loudly.
"""
import struct
import sys

B1_FO, B1 = 0x7300, 0x7b068000
B6_FO, B6 = 0x8d00, 0x7b1a8000


def fo(addr):
    if B1 <= addr < B1 + 0x800:
        return B1_FO + addr - B1
    if B6 <= addr < B6 + 0x800:
        return B6_FO + addr - B6
    raise ValueError(hex(addr))


def expect(d, addr, hexbytes, what):
    got = bytes(d[fo(addr):fo(addr) + len(hexbytes) // 2])
    assert got == bytes.fromhex(hexbytes), '%s: %#x holds %s, expected %s' % (what, addr, got.hex(), hexbytes)


def put(d, addr, blob, end, what):
    assert addr + len(blob) <= end, '%s: %d bytes at %#x overrun %#x' % (what, len(blob), addr, end)
    d[fo(addr):fo(addr) + len(blob)] = blob


def mips16_b(site, target):
    off = (target - (site + 2)) // 2            # B: target = PC + 2 + (imm11 << 1)
    assert -1024 <= off < 1024
    return struct.pack('<H', 0x1000 | (off & 0x7ff))


def mips16_jal(target):
    t = (target & 0x0fffffff) >> 2
    return struct.pack('<HH', 0x1800 | (((t >> 16) & 0x1f) << 5) | ((t >> 21) & 0x1f), t & 0xffff)


def main(src, out, shuffle_p, *playall):
    d = bytearray(open(src, 'rb').read())
    rd = lambda p: open(p, 'rb').read()

    # shuffle fix
    expect(d, 0x7b1a808c, 'd464', 'stock init_shuffle prologue')
    expect(d, 0x7b1a83ec, '9164', 'shuffle history function')
    put(d, 0x7b1a808c, rd(shuffle_p), 0x7b1a82ac, 'shuffle blob')
    d[fo(0x7b1a84b0):fo(0x7b1a84b0) + 2] = mips16_b(0x7b1a84b0, 0x7b1a817c)
    print('shuffle fix: %d bytes at 0x7b1a808c, picker -> b 0x7b1a817c' % len(rd(shuffle_p)))

    if playall:
        a, b, c = (rd(p) for p in playall)
        expect(d, 0x7b068484, 'd664', 'dead disk counter prologue')
        expect(d, 0x7b06856c, 'd564', 'folder get_location prologue')
        expect(d, 0x7b1a8568, 'd464', 'function after the dead picker')
        expect(d, 0x7b068088, '36187aa0', 'Repeat All jal 0x7b0681e8')
        put(d, 0x7b068484, a, 0x7b06856c, 'unit A')
        put(d, 0x7b1a82ac, b, 0x7b1a83ec, 'unit B')
        put(d, 0x7b1a84b4, c, 0x7b1a8568, 'unit C')
        d[fo(0x7b068088):fo(0x7b068088) + 4] = mips16_jal(0x7b1a84b4)
        print('whole-card shuffle: A %d B, B %d B, C %d B, hook jal 0x7b1a84b4' % (len(a), len(b), len(c)))

    open(out, 'wb').write(d)


if __name__ == '__main__':
    if len(sys.argv) not in (4, 7):
        sys.exit(__doc__)
    main(*sys.argv[1:])
