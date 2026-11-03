"""Patch the MUSIC.LIB builder for whole-card libraries.

usage: patch_library.py playlist.ap LOADER.bin INDEX.bin OUT.ap N_MAX [PREALLOC_SONGS]
       patch_library.py --reader APP.ap OUT.ap N_MAX

playlist.ap: raises the two scan limits from 4,000 to N_MAX, grows the MUSIC.LIB
preallocation to 64 KB plus 660 bytes per song, makes FUN_40194c78 keep an existing
library that is smaller than that (stock closes it and creates the name again, which on an
exFAT card adds a second MUSIC.LIB entry; deleting it first on that path changed nothing,
and writes past the end of the old file extend it), ends the scan's counting pass
at once (it only sized the stock 64 KB key buffer, and the new passes never read the keys;
that saves one walk of the whole card), puts the loader (loader.c) over the stock
index-pass driver FUN_400d5290, and stores the passes (index.c) as data in free bank space:

  chunk 0: bank 0x400d4c00, offset 0x000 to 0x690 (the stock passes, now dead)
  chunk 1: bank 0x71252500 (unused), a "jr ra; nop" stub then 2,044 bytes
  chunk 2: bank 0x400d4c00, offset 0x770 to 0x800 (after the loader)
  chunk 3: the zero tail of bank 0x48094400 (clears the update flag when the build is done)
           from offset 0x2a0, a stub then 1,372 bytes
  chunk 4: the zero tail of bank 0x48594400 (user playlist file helpers) from offset 0x2c0,
           a stub then 1,340 bytes

Two traps.  The zero tail of bank 0x71292500 looks free but is not: the M4A tag reader's
UTF-8 string helper keeps its two scratch buffers there (0x7129272c and 0x712927ac), and it
relies on them being zero past the string, so code stored there turns up as junk after short
M4A tag names.  Chunk 3 lived there in the first whole-card builds.  And every 0x40xx4c00
bank is loaded into the one 2 KB window that holds the loader's own bank 0x400d4c00, so a
chunk in another bank of that group (chunk 3 was in 0x40254c00 for one build) replaces the
loader while it runs and the build hangs at 20%.  Chunks outside the loader's bank must be
in the 0x48 or 0x71 groups, which have their own windows.

The loader copies them in that order to 0xbfc3c000.  The layout here and in loader.c must
agree.  --reader patches the five 4,000 limits in the song-lookup functions of music.ap,
browser.ap, or mainmenu.ap (banks 0x712d2500 and 0x71352500).
"""
import struct
import sys

LOADER_FO, LOADER_MAX = 0x3a90, 0xe0
CHUNKS = (  # (file offset of the data, capacity, stub file offset or None)
    (0x3400, 0x690, None),
    (0x10404, 0x7f8, 0x10400),
    (0x3b70, 0x90, None),
    (0x86a4, 0x55c, 0x86a0),
    (0x166c4, 0x53c, 0x166c0),
)
CHUNK_BANKS = {0x3400: 0x400d4c00, 0x10400: 0x71252500, 0x8400: 0x48094400, 0x16400: 0x48594400}
SCAN_LIMITS = (0x323e, 0x32c8)             # EXTEND + sltiu v0,4000 in FUN_40095220
PREALLOC = 0x5260                          # u32 0x281000 in the MUSIC.LIB size table
KEEP_SMALL = 0x4d0e                        # FUN_40194c78: 'bteqz' past the close when the file is big enough
FIRST_WALK = 0x322a                        # FUN_40095220: 'jalrc' of the counting pass's first walker call
READER_SITES = {                           # EXTEND + sltiu/cmpi 4000 in the lookup functions
    'browser': (0xe626, 0xe682, 0xf652, 0xf6be, 0xf8dc),
    'mainmenu': (0xfd26, 0xfd82, 0x10852, 0x108be, 0x10adc),
    'music': (0x19626, 0x19682, 0x1a152, 0x1a1be, 0x1a3dc),
}
STUB = bytes.fromhex('20e80065')           # MIPS16e: jr ra; nop


def extend_imm(d, off, new):
    """Rewrite the 16-bit immediate of an EXTENDed MIPS16e sltiu/cmpi at off."""
    e, i = struct.unpack_from('<HH', d, off)
    assert e & 0xf800 == 0xf000, 'no EXTEND at %#x' % off
    assert (i >> 11) in (0x0b, 0x0e), 'not sltiu/cmpi at %#x' % off
    old = ((e & 0x1f) << 11) | (e & 0x7e0) | (i & 0x1f)
    assert old == 4000, 'immediate at %#x is %d, not 4000' % (off, old)
    e = 0xf000 | (new & 0x7e0) | ((new >> 11) & 0x1f)
    i = (i & 0xffe0) | (new & 0x1f)
    struct.pack_into('<HH', d, off, e, i)


def patch_playlist(src, loader, index, out, nmax, prealloc_n=None):
    if prealloc_n is None:
        prealloc_n = nmax
    d = bytearray(open(src, 'rb').read())
    ld = open(loader, 'rb').read()
    blob = open(index, 'rb').read()
    cap = sum(c[1] for c in CHUNKS)
    assert len(ld) <= LOADER_MAX, 'loader is %d bytes, room for %d' % (len(ld), LOADER_MAX)
    assert len(blob) <= cap, 'index passes are %d bytes, room for %d' % (len(blob), cap)
    assert struct.unpack_from('<I', d, PREALLOC)[0] == 0x281000, 'preallocation literal not found'
    assert d[LOADER_FO:LOADER_FO + 2] == bytes.fromhex('d464'), 'FUN_400d5290 not at the loader site'
    assert d[KEEP_SMALL:KEEP_SMALL + 2] == bytes.fromhex('4d60'), 'FUN_40194c78 size-check branch not found'
    assert d[FIRST_WALK:FIRST_WALK + 2] == bytes.fromhex('c0ea'), 'FUN_40095220 first walker call not found'
    assert not any(d[0x1040c:0x10c00]), 'bank 0x71252500 is not empty'
    assert not any(d[0x86a0:0x8c00]), 'bank 0x48094400 tail is not free'
    assert not any(d[0x166c0:0x16c00]), 'bank 0x48594400 tail is not free'
    table = [struct.unpack_from('<3I', d, off) for off in range(0x100, 0x1000, 12)]
    table = [t for t in table if t[0] and t[1] and t[2] <= 0x800]     # the real bank entries
    for fo, addr in CHUNK_BANKS.items():
        assert (fo, addr, 0x800) in table, 'bank at file %#x is not %#x' % (fo, addr)
    for fo, size, stub in CHUNKS:
        lo = stub if stub is not None else fo
        bank = next(a for f, a, n in table if f <= lo and lo + size <= f + n)
        assert bank == 0x400d4c00 or bank >> 24 in (0x48, 0x71), 'chunk at %#x shares the loader window' % fo
    for off in SCAN_LIMITS:
        extend_imm(d, off, nmax)
    struct.pack_into('<I', d, PREALLOC, 0x10000 + 660 * prealloc_n)
    d[KEEP_SMALL:KEEP_SMALL + 2] = bytes.fromhex('4d10')     # b: keep the open file whatever its size
    d[FIRST_WALK:FIRST_WALK + 2] = bytes.fromhex('006a')     # li v0,0: the counting pass ends at once
    d[LOADER_FO:LOADER_FO + LOADER_MAX] = ld.ljust(LOADER_MAX, b'\0')
    pos = 0
    for fo, size, stub in CHUNKS:
        piece = blob[pos:pos + size]
        d[fo:fo + size] = piece.ljust(size, b'\0')
        if stub is not None:
            d[stub:stub + 4] = STUB
        pos += size
    back = b''.join(bytes(d[fo:fo + size]) for fo, size, stub in CHUNKS)
    assert back[:len(blob)] == blob and not any(back[len(blob):]), 'chunk read-back differs from the blob'
    open(out, 'wb').write(d)
    print('playlist.ap: scan limit %d, MUSIC.LIB preallocation %d bytes, loader %d bytes, passes %d of %d bytes'
          % (nmax, 0x10000 + 660 * prealloc_n, len(ld), len(blob), cap))


def patch_reader(src, out, nmax):
    d = bytearray(open(src, 'rb').read())
    name = next(n for n in READER_SITES if src.endswith(n + '.ap'))
    for off in READER_SITES[name]:
        extend_imm(d, off, nmax)
    open(out, 'wb').write(d)
    print('%s.ap: %d song-lookup limits raised to %d' % (name, len(READER_SITES[name]), nmax))


if __name__ == '__main__':
    if sys.argv[1] == '--reader':
        patch_reader(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    else:
        patch_playlist(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]),
                       int(sys.argv[6]) if len(sys.argv) > 6 else None)
