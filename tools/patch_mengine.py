"""Patch mengine.ap (2025-06-26 firmware) with the shuffle fix and, optionally, whole-card shuffle.

usage: patch_mengine.py STOCK_mengine.ap OUT.ap SHUFFLE.bin [A.bin B.bin C.bin D.bin E.bin]

Every edit lives in mengine's bank group 0x7b, whose {file offset, address, size} entries sit
at 0x700 of the .ap file, past the part of the bank table that the other groups use:
  bank 1: file 0x7300 -> 0x7b068000   bank 2: file 0x7b00 -> 0x7b0a8000   bank 6: file 0x8d00 -> 0x7b1a8000

Shuffle fix (tools/shuffle):
  SHUFFLE.bin over the stock init_shuffle at 0x7b1a808c (ends at 0x7b1a82ac), and the stock
  picker entry 0x7b1a84b0 becomes `b 0x7b1a817c` (the new shuffle_next), leaving the rest of
  the old picker body dead.
Whole-card shuffle (tools/playall):
  A (walker)       over the dead disk-wide counter 0x7b068484, must end before 0x7b06856c
                   (live folder-mode get_location, called through the file-selector table)
  B (folder perm)  in the free tail 0x7b1a82ac..0x7b1a83ec
  C (playall_next) in the dead picker body 0x7b1a84b4..0x7b1a8568
  D (folder_prev, folder_song) in bank 6's zero tail from 0x7b1a8754
  E (playall_prev) in bank 2's zero tail from 0x7b0a8458
  hooks: the Repeat All next call `jal 0x7b0681e8` at 0x7b068088 becomes `jal 0x7b1a84b4`, and
  both Repeat All prev calls `jal 0x7b0a82ac` (0x7b0a80de, 0x7b0a812e) become `jal 0x7b0a8458`.
  Banks 2 and 6 load only their table size, so the size grows to cover D and E. The new size
  stays within 0x7c4, the largest bank in the group (so the shared load window holds it),
  and before the next bank's file offset.
Every edit checks the stock bytes first, so a different firmware version fails loudly.
"""
import struct
import sys

B1_FO, B1 = 0x7300, 0x7b068000
B2_FO, B2 = 0x7b00, 0x7b0a8000
B6_FO, B6 = 0x8d00, 0x7b1a8000
GROUP_MAX = 0x7c4                               # largest bank in group 0x7b (bank 4)
BANK_ENTRY = {B2: 0x718, B6: 0x748}             # {file offset, address, size} table entries
BANK_FO = {B2: B2_FO, B6: B6_FO}
BANK_SIZE = {B2: 0x458, B6: 0x754}
NEXT_BANK_FO = {B2: 0x8000, B6: 0x9500}


def fo(addr):
    for base, f in ((B1, B1_FO), (B2, B2_FO), (B6, B6_FO)):
        if base <= addr < base + 0x800:
            return f + addr - base
    raise ValueError(hex(addr))


def strip_pad(blob):
    while blob.endswith(b'\x00\x65'):          # MIPS16 nop padding after the last pool
        blob = blob[:-2]
    return blob


def grow_bank(d, base, addr, blob, what):
    """Put blob in the zero tail of a bank and raise the bank's load size to cover it."""
    e = BANK_ENTRY[base]
    f, a, size = struct.unpack_from('<3I', d, e)
    assert (f, a, size) == (BANK_FO[base], base, BANK_SIZE[base]), \
        '%s: bank table entry at %#x is %#x %#x %#x' % (what, e, f, a, size)
    start = addr - base
    assert start >= size and start % 4 == 0
    new = (start + len(blob) + 3) & ~3
    assert new <= GROUP_MAX and BANK_FO[base] + new <= NEXT_BANK_FO[base], \
        '%s: %d bytes at %#x need bank size %#x' % (what, len(blob), addr, new)
    tail = d[BANK_FO[base] + size:NEXT_BANK_FO[base]]
    assert not any(tail), '%s: bank tail is not empty' % what
    d[fo(addr):fo(addr) + len(blob)] = blob
    struct.pack_into('<I', d, e + 8, new)
    return new


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
        a, b, c, dd, e = (rd(p) for p in playall)
        expect(d, 0x7b068484, 'd664', 'dead disk counter prologue')
        expect(d, 0x7b06856c, 'd564', 'folder get_location prologue')
        expect(d, 0x7b1a8568, 'd464', 'function after the dead picker')
        expect(d, 0x7b068088, '36187aa0', 'Repeat All jal 0x7b0681e8')
        expect(d, 0x7b0a82ac, 'd564', 'stock prev_cross prologue')
        for site in (0x7b0a80de, 0x7b0a812e):
            expect(d, site, '5618aba0', 'Repeat All jal 0x7b0a82ac')
        put(d, 0x7b068484, a, 0x7b06856c, 'unit A')
        put(d, 0x7b1a82ac, b, 0x7b1a83ec, 'unit B')
        put(d, 0x7b1a84b4, c, 0x7b1a8568, 'unit C')
        s6 = grow_bank(d, B6, 0x7b1a8754, strip_pad(dd), 'unit D')
        s2 = grow_bank(d, B2, 0x7b0a8458, strip_pad(e), 'unit E')
        d[fo(0x7b068088):fo(0x7b068088) + 4] = mips16_jal(0x7b1a84b4)
        for site in (0x7b0a80de, 0x7b0a812e):
            d[fo(site):fo(site) + 4] = mips16_jal(0x7b0a8458)
        print('whole-card shuffle: A %d B, B %d B, C %d B, hook jal 0x7b1a84b4' % (len(a), len(b), len(c)))
        print('shuffle-all prev: D %d B (bank 6 size %#x -> %#x), E %d B (bank 2 size %#x -> %#x), '
              'hooks jal 0x7b0a8458' % (len(strip_pad(dd)), BANK_SIZE[B6], s6, len(strip_pad(e)), BANK_SIZE[B2], s2))

    open(out, 'wb').write(d)


if __name__ == '__main__':
    if len(sys.argv) not in (4, 9):
        sys.exit(__doc__)
    main(*sys.argv[1:])
