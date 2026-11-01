#!/usr/bin/env python3
"""Create and apply BPS patches.

Releases ship a BPS patch against the stock MECHEN_M30.HEX instead of the patched firmware,
so the repository never distributes MECHEN's code. BPS stores CRC-32s of the source, the
target, and the patch itself, so applying a patch to the wrong file fails instead of writing
a bad HEX. Any BPS tool works (Rom Patcher JS, Floating IPS, beat); this one needs only Python.

  bps.py create STOCK.HEX PATCHED.HEX OUT.bps
  bps.py apply  STOCK.HEX PATCH.bps   OUT.HEX
"""
import sys
import zlib

MAGIC = b'BPS1'
SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY = range(4)


def encode(n):
    out = bytearray()
    while True:
        x = n & 0x7f
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return bytes(out)
        out.append(x)
        n -= 1


def crc(data):
    return (zlib.crc32(data) & 0xffffffff).to_bytes(4, 'little')


def mismatches(src, tgt, chunk=4096):
    """Offsets where tgt differs from src at the same offset, or runs past the end of src."""
    n = min(len(src), len(tgt))
    for base in range(0, n, chunk):
        a, b = src[base:base + chunk], tgt[base:base + chunk]
        if a != b:
            yield from (base + k for k in range(len(b)) if a[k] != b[k])
    yield from range(n, len(tgt))


def create(src, tgt, gap=8):
    """Literal runs for changed bytes (merged across gaps shorter than `gap`), SourceRead elsewhere."""
    spans = []
    for i in mismatches(src, tgt):
        if spans and i - spans[-1][1] < gap:
            spans[-1][1] = i + 1
        else:
            spans.append([i, i + 1])
    out = bytearray(MAGIC + encode(len(src)) + encode(len(tgt)) + encode(0))
    pos = 0
    for start, end in spans + [[len(tgt), len(tgt)]]:
        if start > pos:
            out += encode(((start - pos - 1) << 2) | SOURCE_READ)
        if end > start:
            out += encode(((end - start - 1) << 2) | TARGET_READ) + tgt[start:end]
        pos = end
    out += crc(src) + crc(tgt)
    out += crc(out)
    return bytes(out)


def apply(src, patch):
    if patch[:4] != MAGIC or len(patch) < 19:
        raise ValueError('not a BPS patch')
    if crc(patch[:-4]) != patch[-4:]:
        raise ValueError('patch is damaged (patch CRC-32 mismatch)')
    if crc(src) != patch[-12:-8]:
        raise ValueError('wrong source file (source CRC-32 mismatch); use the stock firmware the patch was made for')
    p = 4
    end = len(patch) - 12

    def decode():
        nonlocal p
        data, shift = 0, 1
        while True:
            x = patch[p]
            p += 1
            data += (x & 0x7f) * shift
            if x & 0x80:
                return data
            shift <<= 7
            data += shift

    src_size, tgt_size, meta_size = decode(), decode(), decode()
    p += meta_size
    if src_size != len(src):
        raise ValueError('wrong source file (size mismatch)')
    out = bytearray()
    src_rel = tgt_rel = 0
    while p < end:
        data = decode()
        cmd, length = data & 3, (data >> 2) + 1
        if cmd == SOURCE_READ:
            out += src[len(out):len(out) + length]
        elif cmd == TARGET_READ:
            out += patch[p:p + length]
            p += length
        else:
            d = decode()
            delta = -(d >> 1) if d & 1 else d >> 1
            if cmd == SOURCE_COPY:
                src_rel += delta
                out += src[src_rel:src_rel + length]
                src_rel += length
            else:
                tgt_rel += delta
                for _ in range(length):
                    out.append(out[tgt_rel])
                    tgt_rel += 1
    if len(out) != tgt_size or crc(out) != patch[-8:-4]:
        raise ValueError('patched output failed its CRC-32 check')
    return bytes(out)


def main(argv):
    if len(argv) != 5 or argv[1] not in ('create', 'apply'):
        sys.exit(__doc__.strip())
    _, cmd, a, b, out = argv
    with open(a, 'rb') as f:
        src = f.read()
    with open(b, 'rb') as f:
        other = f.read()
    try:
        result = create(src, other) if cmd == 'create' else apply(src, other)
    except ValueError as e:
        sys.exit(f'{argv[0]}: {e}')
    if cmd == 'create' and apply(src, result) != other:
        sys.exit(f'{argv[0]}: internal error: patch does not reproduce {b}')
    with open(out, 'wb') as f:
        f.write(result)
    print(f'wrote {out} ({len(result)} bytes)')


if __name__ == '__main__':
    main(sys.argv)
