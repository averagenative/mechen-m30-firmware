"""Make QEMU test copies of the dirlist bank (stock and patched) for tools/scroll/harness.c.

usage: testbank.py browser.ap TEST_BLOB.bin ACCEL_NEXT ACCEL_PREV OUTDIR
Writes OUTDIR/bank_stock_test.bin and OUTDIR/bank_patched_test.bin.  Both have the bank's
literal-pool pointers moved from player RAM / resident stubs to the harness's test RAM and
stubs; the patched copy also gets the test blob and the two hook branches.
"""
import struct
import sys

BANK, BANK_FO, FREE = 0x48714400, 0x18900, 0x680
SITES = (0x487144a8, 0x487146cc)
REDIRECT = {0x9fc15634: 0x10000034, 0x9fc15636: 0x10000036, 0xbfc16a21: 0x00700100}


def redirect(bank):
    b = bytearray(bank)
    for o in range(0, FREE, 4):
        new = REDIRECT.get(struct.unpack_from('<I', b, o)[0])
        if new:
            struct.pack_into('<I', b, o, new)
    return b


def hook(bank, blob, targets):
    b = bytearray(bank)
    for site, tgt in zip(SITES, targets):
        off = (tgt - (site + 2)) // 2
        assert -1024 <= off < 1024
        struct.pack_into('<H', b, site - BANK, 0x1000 | (off & 0x7ff))
    assert not any(b[FREE:0x800])
    b[FREE:FREE + len(blob)] = blob
    return b


def main(app, blob_p, nx, pv, out):
    bank = open(app, 'rb').read()[BANK_FO:BANK_FO + 0x800]
    open(out + '/bank_stock_test.bin', 'wb').write(redirect(bank))
    patched = hook(redirect(bank), open(blob_p, 'rb').read(), (int(nx, 16), int(pv, 16)))
    open(out + '/bank_patched_test.bin', 'wb').write(patched)


if __name__ == '__main__':
    main(*sys.argv[1:6])
