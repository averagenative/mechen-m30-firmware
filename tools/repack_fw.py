"""Rebuild an Actions ATJ2127 FWU (.HEX) with replaced files inside FWIMAGE.FW.

usage: repack_fw.py ORIG.HEX ORIG_DECRYPTED_AFI.bin OUT.HEX [name.ext=replacement ...]

Layers, inside out (all checksums are recomputed):
  FWIMAGE.FW (f0 variant): file data, per-entry word sum, directory sum @0x10, u16 header sum @0x1fe
  AFI container:           FWIMAGE.FW entry word sum, post-header word sum @0xffc
  FWU:                     ATJ2127 sector cipher (session key recovered from the original),
                           FwuTail word sum over the file up to fw_size
With no replacements the output must be byte-identical to ORIG.HEX.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fwu_repack import deinterleave, derive_key, encrypt_sector, layout  # noqa: E402


def sum32(b):
    return sum(struct.unpack('<%dI' % (len(b) // 4), b[:len(b) // 4 * 4])) & 0xffffffff


def sum16(b):
    return sum(struct.unpack('<%dH' % (len(b) // 2), b)) & 0xffff


def fw_entries(fw):
    for off in range(0x200, 0x2000, 32):
        name = fw[off:off + 8]
        if name[0] == 0:
            continue
        nm = name.decode('ascii', 'replace').rstrip()
        ext = fw[off + 8:off + 11].decode('ascii', 'replace').rstrip()
        blk, size, nbytes, chk = struct.unpack_from('<4I', fw, off + 16)
        yield ('%s.%s' % (nm, ext)).lower(), off, blk << 9, size, nbytes, chk


def afi_entry(afi, wanted):
    for i in range(126):
        off = 0x20 + i * 32
        name = afi[off:off + 8].decode('ascii', 'replace').rstrip()
        ext = afi[off + 8:off + 11].decode('ascii', 'replace').rstrip()
        if ('%s.%s' % (name, ext)).lower() == wanted:
            addr, foff, size = struct.unpack_from('<3I', afi, off + 12)
            return off, foff, size
    raise KeyError(wanted)


def main(orig_hex, orig_afi, out_hex, replacements):
    orig = open(orig_hex, 'rb').read()
    afi0 = open(orig_afi, 'rb').read()
    afi = bytearray(afi0)

    ent_off, fw_off, fw_size = afi_entry(afi, 'fwimage.fw')
    fw = bytearray(afi[fw_off:fw_off + fw_size])
    entries = {e[0]: e for e in fw_entries(fw)}
    for name, path in replacements.items():
        _, doff, data_off, size, nbytes, _ = entries[name]
        new = open(path, 'rb').read()
        if len(new) != size:
            raise SystemExit('%s: size %d != slot %d (resizing not supported)' % (name, len(new), size))
        fw[data_off:data_off + size] = new
        struct.pack_into('<I', fw, doff + 28, sum32(fw[data_off:data_off + size]))
        print('replaced %-12s entry checksum %#010x' % (name, sum32(fw[data_off:data_off + size])))
    struct.pack_into('<I', fw, 0x10, sum32(fw[0x200:0x2000]))
    struct.pack_into('<H', fw, 0x1fe, sum16(fw[:510]))

    afi[fw_off:fw_off + fw_size] = fw
    struct.pack_into('<I', afi, ent_off + 28, sum32(fw))
    struct.pack_into('<I', afi, 0xffc, sum32(afi[:0xffc]))

    fwu_size, off_a, off_b = layout(orig)
    ct_orig, block_a, block_b = deinterleave(orig)
    assert len(afi) == len(ct_orig), (len(afi), len(ct_orig))
    k0, sk7 = derive_key(ct_orig, afi0)
    rounds = max(1, 16 - (len(orig) >> 0x19))
    ct = b''.join(encrypt_sector(bytes(afi[i:i + 512]), k0, sk7, rounds) for i in range(0, len(afi), 512))
    out = bytearray(orig[:512] + ct[:off_a] + block_a + ct[off_a:off_a + off_b] + block_b
                    + ct[off_a + off_b:] + orig[fwu_size:])
    struct.pack_into('<I', out, fwu_size + 0x10, sum32(out[:fwu_size]))
    open(out_hex, 'wb').write(out)
    print('wrote %s (%d bytes)' % (out_hex, len(out)))


if __name__ == '__main__':
    reps = dict(a.split('=', 1) for a in sys.argv[4:])
    main(sys.argv[1], sys.argv[2], sys.argv[3], reps)
