#!/usr/bin/env python3
"""Decompress a BLZ-packed 3DS .code section.

Nintendo's BLZ works backwards from the end of the file, which is why a packed
code.bin still opens with recognisable ARM instructions -- only the tail is
compressed. An 8-byte footer says how much.
"""
import struct
import sys


def blz_decompress(buf):
    buf = bytearray(buf)
    n = len(buf)

    hdr = struct.unpack_from("<I", buf, n - 8)[0]
    add = struct.unpack_from("<I", buf, n - 4)[0]
    hdr_len = hdr >> 24
    comp_len = hdr & 0x00FFFFFF

    if hdr_len == 0 or comp_len == 0 or comp_len > n:
        raise SystemExit("does not look BLZ-compressed (hdr=%#x add=%#x)" % (hdr, add))

    out = bytearray(n + add)
    out[:n] = buf

    src = n - hdr_len       # read pointer, moving down
    dst = n + add           # write pointer, moving down
    end = n - comp_len

    while src > end:
        src -= 1
        flags = out[src]
        for _ in range(8):
            if flags & 0x80:
                # A little-endian u16: top nibble is the length, low 12 bits
                # the distance. Both are biased by 3.
                src -= 2
                v = out[src] | (out[src + 1] << 8)
                length = (v >> 12) + 3
                pos = (v & 0x0FFF) + 3
                for _ in range(length):
                    dst -= 1
                    out[dst] = out[dst + pos]
            else:
                src -= 1
                dst -= 1
                out[dst] = out[src]
            flags = (flags << 1) & 0xFF
            if src <= end:
                break

    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit("usage: blz.py <code.bin> <out.bin>")
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    out = blz_decompress(data)
    with open(sys.argv[2], "wb") as f:
        f.write(out)
    print("%d -> %d bytes" % (len(data), len(out)))
