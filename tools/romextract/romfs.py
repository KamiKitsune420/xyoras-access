#!/usr/bin/env python3
"""Walk a RomFS image and pull out files by extension.

RomFS is an IVFC container; the filesystem proper is level 3. Reads only the
metadata tables and the requested files, so a 1.8 GB image costs nothing to
search.
"""
import os
import struct
import sys


def u32(f, off):
    f.seek(off)
    return struct.unpack("<I", f.read(4))[0]


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def level3_offset(f):
    """Where the actual filesystem starts inside the IVFC container.

    The hash-level arithmetic in the IVFC header is easy to get subtly wrong,
    and getting it wrong yields a plausible-looking offset full of garbage. The
    level 3 header is self-identifying instead: it opens with its own length,
    always 0x28, followed by six table offset/length pairs that must point
    inside the image and rise monotonically. Scanning block-aligned offsets for
    that shape is slower to write but cannot silently land in the wrong place.
    """
    f.seek(0, 2)
    size = f.tell()

    f.seek(0)
    is_ivfc = f.read(4) == b"IVFC"
    if not is_ivfc:
        return 0

    for base in range(0x1000, min(size, 0x400000), 0x1000):
        f.seek(base)
        head = f.read(0x28)
        if len(head) < 0x28:
            break
        fields = struct.unpack("<10I", head[:40])
        if fields[0] != 0x28:
            continue
        # dirHash, dirTable, fileHash, fileTable offsets and the data offset
        offs = [fields[1], fields[3], fields[5], fields[7], fields[9]]
        if any(o >= size - base for o in offs):
            continue
        if offs != sorted(offs):
            continue
        return base

    raise SystemExit("could not locate the level 3 RomFS header")


def walk(path, want_ext, outdir, limit=None):
    with open(path, "rb") as f:
        base = level3_offset(f)
        hdr_len = u32(f, base)
        file_meta_off = u32(f, base + 0x1C)
        file_meta_len = u32(f, base + 0x20)
        file_data_off = u32(f, base + 0x24)

        print("romfs level3 @ %#x  header %d bytes" % (base, hdr_len))
        print("file metadata @ %#x (%d bytes), data @ %#x"
              % (file_meta_off, file_meta_len, file_data_off))

        os.makedirs(outdir, exist_ok=True)

        # Walk the file metadata table. Each entry: parentDir, nextSibling,
        # dataOffset(u64), dataSize(u64), nextHash, nameLen, then the UTF-16 name.
        pos = 0
        found = 0
        while pos < file_meta_len:
            f.seek(base + file_meta_off + pos)
            ent = f.read(0x20)
            if len(ent) < 0x20:
                break
            data_off, data_size = struct.unpack_from("<QQ", ent, 8)
            name_len = struct.unpack_from("<I", ent, 0x1C)[0]
            name = f.read(name_len).decode("utf-16-le", "replace") if name_len else ""

            if name.lower().endswith(want_ext):
                found += 1
                f.seek(base + file_data_off + data_off)
                out = os.path.join(outdir, name)
                with open(out, "wb") as o:
                    o.write(f.read(data_size))
                print("  %-32s %8d bytes" % (name, data_size))
                if limit and found >= limit:
                    break

            step = 0x20 + name_len
            pos += align(step, 4)

        print("\n%d file(s) matching %s" % (found, want_ext))


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit("usage: romfs.py <romfs.bin> <.ext> <outdir>")
    walk(sys.argv[1], sys.argv[2].lower(), sys.argv[3])
