"""Re-encrypt a decrypted ATJ2127 FWU payload using the session key and
crypto blocks of an original FWU file (MECHEN_M30.HEX).

usage: fwu_repack.py ORIGINAL.HEX DECRYPTED_PAYLOAD.bin OUT.HEX
"""
import struct, sys

ATJ2127_KEY = [0x42146ea2, 0x892c8e85, 0x9f9f6d27, 0x545fedc3,
               0x09e5c0ca, 0x2dfa7e61, 0x4e5322e6, 0xb19185b9]

def words(b):
    return list(struct.unpack('<%dI' % (len(b) // 4), b))

def layout(orig):
    fw_size = struct.unpack_from('<I', orig, 16)[0]
    off_a = 512 * (1 + (orig[0x1ee] & 15))
    off_b = 512 * (1 + (orig[0x1fe] & 15))
    return fw_size, off_a, off_b

def deinterleave(orig):
    """Return (payload ciphertext, blockA, blockB) in decrypt order."""
    fw_size, off_a, off_b = layout(orig)
    size = fw_size - 2048
    a = 512 + off_a
    b = a + 1024 + off_b
    ct = orig[512:a] + orig[a + 1024:b] + orig[b + 512:b + 512 + size - off_a - off_b]
    return ct, orig[a:a + 1024], orig[b:b + 512]

def derive_key(ct, pt):
    """Known-plaintext: recover per-sector initial key words and session_key[7]."""
    c, p = words(ct[:32]), words(pt[:32])
    k = [None] + [c[i] ^ p[i] for i in range(7)]
    assert c[7] ^ p[7] == k[1] ^ k[4], "keystream check failed"
    sk7 = k[7] ^ ATJ2127_KEY[7]
    return k, sk7

def encrypt_sector(pt, k0, sk7, rounds):
    k = list(k0)
    p = words(pt)
    out = list(p)
    for r in range(min(rounds, len(p) // 8)):
        w = p[r * 8:r * 8 + 8]
        c = [w[0] ^ k[1], w[1] ^ k[2], w[2] ^ k[3], w[3] ^ k[4],
             w[4] ^ k[5], w[5] ^ k[6], w[6] ^ k[7], w[7] ^ k[1] ^ k[4]]
        out[r * 8:r * 8 + 8] = c
        k = [None, k[2], k[3], k[4], k[5], k[6], k[7], c[7] ^ sk7]
    return struct.pack('<%dI' % len(out), *out)

def main(orig_path, pt_path, out_path):
    orig = open(orig_path, 'rb').read()
    pt = open(pt_path, 'rb').read()
    fw_size, off_a, off_b = layout(orig)
    ct_orig, block_a, block_b = deinterleave(orig)
    assert len(pt) == len(ct_orig) == fw_size - 2048, (len(pt), len(ct_orig))
    k0, sk7 = derive_key(ct_orig, pt)
    rounds = max(1, 16 - (len(orig) >> 0x19))
    ct = b''.join(encrypt_sector(pt[i:i + 512], k0, sk7, rounds)
                  for i in range(0, len(pt), 512))
    out = (orig[:512] + ct[:off_a] + block_a + ct[off_a:off_a + off_b] + block_b
           + ct[off_a + off_b:] + orig[fw_size:])
    open(out_path, 'wb').write(out)
    print('wrote', out_path, len(out), 'bytes; trailer', len(orig) - fw_size, 'bytes reused')

if __name__ == '__main__':
    main(*sys.argv[1:4])
