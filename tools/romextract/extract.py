#!/usr/bin/env python3
"""Pull ExeFS and RomFS out of a decrypted 3DS ROM.

Written because neither 3dstool nor ctrtool is installed here, and the ROM is
already decrypted (NoCrypto set), so the containers can just be parsed.

Only reads. Nothing is written back to the ROM.
"""
import os
import struct
import sys

MEDIA = 0x200


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def find_ncch(f):
    """Partition 0 of an NCSD, or the file itself if it is already an NCCH."""
    f.seek(0x100)
    magic = f.read(4)
    if magic == b"NCCH":
        return 0
    if magic != b"NCSD":
        raise SystemExit("not an NCSD or NCCH: %r" % magic)
    f.seek(0x120)
    off, _size = struct.unpack("<II", f.read(8))
    return off * MEDIA


def read_ncch(f, base):
    f.seek(base + 0x100)
    hdr = f.read(0x100)
    if hdr[0:4] != b"NCCH":
        raise SystemExit("no NCCH at %#x" % base)
    flags = hdr[0x88:0x90]
    return {
        "product": hdr[0x50:0x60].decode("ascii", "replace").rstrip("\0"),
        "exefs_off": u32(hdr, 0xA0) * MEDIA,
        "exefs_size": u32(hdr, 0xA4) * MEDIA,
        "romfs_off": u32(hdr, 0xB0) * MEDIA,
        "romfs_size": u32(hdr, 0xB4) * MEDIA,
        "nocrypto": bool(flags[7] & 0x04),
        "exefs_compressed": bool(hdr[0x0D] & 0x01),  # from the ExeFS flags byte
    }


def exefs_files(f, base):
    """The ten-entry ExeFS header: 8-byte name, offset, size."""
    f.seek(base)
    hdr = f.read(0x200)
    out = []
    for i in range(10):
        e = hdr[i * 16:(i + 1) * 16]
        name = e[0:8].rstrip(b"\0").decode("ascii", "replace")
        if not name:
            continue
        off, size = struct.unpack_from("<II", e, 8)
        out.append((name, base + 0x200 + off, size))
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: extract.py <rom> <outdir>")
    rom, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)

    with open(rom, "rb") as f:
        base = find_ncch(f)
        n = read_ncch(f, base)

        print("NCCH at %#x" % base)
        print("  product code : %s" % n["product"])
        print("  decrypted    : %s" % n["nocrypto"])
        print("  ExeFS        : %#x (%d bytes)" % (base + n["exefs_off"], n["exefs_size"]))
        print("  RomFS        : %#x (%d bytes)" % (base + n["romfs_off"], n["romfs_size"]))

        if not n["nocrypto"]:
            raise SystemExit("ROM is encrypted; this tool only handles decrypted dumps")

        print("\nExeFS contents:")
        for name, off, size in exefs_files(f, base + n["exefs_off"]):
            print("  %-10s %9d bytes  @ %#x" % (name, size, off))
            f.seek(off)
            path = os.path.join(outdir, name if name != ".code" else "code.bin")
            with open(path, "wb") as o:
                o.write(f.read(size))

        # RomFS is written whole; the CRO modules are inside it and the IVFC
        # walk is a separate step.
        if n["romfs_size"]:
            f.seek(base + n["romfs_off"])
            with open(os.path.join(outdir, "romfs.bin"), "wb") as o:
                remaining = n["romfs_size"]
                while remaining > 0:
                    chunk = f.read(min(1 << 20, remaining))
                    if not chunk:
                        break
                    o.write(chunk)
                    remaining -= len(chunk)
            print("\nwrote romfs.bin (%d bytes)" % n["romfs_size"])


if __name__ == "__main__":
    main()
