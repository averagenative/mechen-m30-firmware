"""Let play/pause, prev, next and volume work with the screen off (common key filter bank).

usage: patch_keys.py APP.ap KEYPASS.bin OUT.ap
       patch_keys.py --testbanks APP.ap KEYPASS.bin OUTDIR

The filter FUN_48694658 lives in bank 0x48694600 (size 0x600) of browser, music, mainmenu,
playlist and setting; the code is identical in each, only the literal pools differ.
  hook: 0x486947d0 `li v0,255` (first instruction of the screen-off wake path) -> b 0x48694b80
  code: KEYPASS.bin (tools/keys/keypass.S) in the zero tail of the bank at 0x48694b80
Every stock byte the patch depends on is checked first.

--testbanks writes OUTDIR/bank_stock_test.bin and OUTDIR/bank_patched_test.bin for
tools/keys/harness.c: the filter's literal pool pointers (system state, app info, message
send function, idle counters, last key) moved to the harness's test RAM and stub.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from apinfo import parse  # noqa: E402

BANK, SIZE = 0x48694600, 0x600
HOOK, CAVE = 0x486947d0, 0x48694b80
WAKE_PATH, SCREEN_ON_PATH = 0x486947d2, 0x48694806
STOCK = {                       # address: expected stock halfword
    0x486947ce: 0x221b,         # beqz v0,0x48694806   (screen on -> pass the key)
    0x486947d0: 0x6aff,         # li v0,255            (start of the wake path; hooked)
    0x486947d2: 0x4247,         # addiu v0,v0,7        (builds message id 0x106)
    0x48694806: 0x994a,         # lw v0,40(s1)         (screen-on path)
}
POOL = {0x238: 'sys', 0x23c: 'app', 0x240: 'send', 0x244: 'counters', 0x248: 'lastkey'}
TEST = {'sys': 0x10000000, 'app': 0x10000100, 'send': 0x00700000,
        'counters': 0x10000180, 'lastkey': 0x10000200}


def hw(b, addr):
    return struct.unpack_from('<H', b, addr - BANK)[0]


def b_target(site, h):
    assert h & 0xf800 == 0x1000, 'not a MIPS16 B at %#x' % site
    off = h & 0x7ff
    return site + 2 + ((off - 0x800 if off & 0x400 else off) << 1)


def find_bank(app):
    d, _, banks = parse(app)
    hits = [fo for fo, addr, size in banks if addr == BANK and size == SIZE]
    assert len(hits) == 1, 'bank 0x48694600/0x600 not found once in %s' % app
    return bytearray(d), hits[0]


def patch_bank(bank, blob):
    b = bytearray(bank)
    for addr, want in STOCK.items():
        assert hw(b, addr) == want, 'stock %#x: %#06x, expected %#06x' % (addr, hw(b, addr), want)
    code = blob.rstrip(b'\x00\x65')                 # drop the section's trailing nop padding
    assert len(code) == 0x16, 'unexpected keypass size %d' % len(code)
    assert b_target(CAVE + 0x12, struct.unpack_from('<H', blob, 0x12)[0]) == WAKE_PATH
    assert b_target(CAVE + 0x14, struct.unpack_from('<H', blob, 0x14)[0]) == SCREEN_ON_PATH
    assert not any(b[CAVE - 8 - BANK:SIZE]), 'bank tail at 0x48694b78 is not free'
    off = (CAVE - (HOOK + 2)) // 2
    struct.pack_into('<H', b, HOOK - BANK, 0x1000 | (off & 0x7ff))
    b[CAVE - BANK:CAVE - BANK + len(code)] = code
    assert b_target(HOOK, hw(b, HOOK)) == CAVE
    return b


def test_bank(bank):
    b = bytearray(bank)
    vals = {name: struct.unpack_from('<I', b, o)[0] for o, name in POOL.items()}
    assert vals['sys'] == 0x9fc1a1c8 and vals['send'] == 0x48494759 and vals['counters'] == 0x9fc1a180
    remap = {v: TEST[name] for name, v in vals.items()}
    n = 0
    for o in range(0, 0x2a0, 4):
        w = struct.unpack_from('<I', b, o)[0]
        if w in remap:
            struct.pack_into('<I', b, o, remap[w])
            n += 1
    assert n == 11, 'expected 11 literal pool words, found %d' % n
    return b


def main(argv):
    if argv[0] == '--testbanks':
        app, blob_p, outdir = argv[1:4]
        d, fo = find_bank(app)
        bank = d[fo:fo + SIZE]
        blob = open(blob_p, 'rb').read()
        open(os.path.join(outdir, 'bank_stock_test.bin'), 'wb').write(test_bank(bank))
        open(os.path.join(outdir, 'bank_patched_test.bin'), 'wb').write(test_bank(patch_bank(bank, blob)))
        return
    app, blob_p, out = argv[:3]
    d, fo = find_bank(app)
    d[fo:fo + SIZE] = patch_bank(d[fo:fo + SIZE], open(blob_p, 'rb').read())
    open(out, 'wb').write(d)


if __name__ == '__main__':
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(sys.argv[1:])
