"""Insert the scroll-acceleration blob into an app's dirlist bank (0x48714400).

usage: patch_scroll.py APP.ap BANK_FILE_OFFSET BLOB.bin OUT.ap ACCEL_NEXT ACCEL_PREV
  browser.ap: bank at file 0x18900, blob built with TIMER_STUB=0xbfc16a25
  music.ap:   bank at file 0x2d200, blob built with TIMER_STUB=0xbfc16bb1
Blob linked at 0x48714a80 (stock_next tramp, stock_prev tramp, then accel.c); ACCEL_NEXT/PREV are
the linked addresses of accel_next/accel_prev (from nm).  Stock next/prev entries become `b accel_*`.
"""
import struct
import sys

BANK, FREE = 0x48714400, 0x680
SITES = (0x487144a8, 0x487146cc)


def main(src, bank_fo, blob_path, out, acc_next, acc_prev):
    HOOKS = tuple(zip(SITES, (int(acc_next, 16), int(acc_prev, 16))))
    d = bytearray(open(src, 'rb').read())
    blob = open(blob_path, 'rb').read()
    b = int(bank_fo, 0)
    for site, _ in HOOKS:
        assert d[b + site - BANK:b + site - BANK + 2] == b'\xd3\x64', 'unexpected stock prologue'
    assert FREE + len(blob) <= 0x800 and not any(d[b + FREE:b + 0x800]), 'bank tail not free'
    for site, tgt in HOOKS:
        off = (tgt - (site + 2)) // 2
        assert -1024 <= off < 1024
        struct.pack_into('<H', d, b + site - BANK, 0x1000 | (off & 0x7ff))
    d[b + FREE:b + FREE + len(blob)] = blob
    open(out, 'wb').write(d)


if __name__ == '__main__':
    main(*sys.argv[1:7])
