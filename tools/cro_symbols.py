#!/usr/bin/env python3
"""Dump the symbol tables of a Nintendo 3DS CRO module.

Generation 6 splits its game logic across 84 CRO modules (see
"AI docks/04-gen6-reverse-engineering.md"). Finding anything inside one means
knowing what it contains, and CROs carry three useful tables:

  exports  — symbols this module offers. Game Freak exports almost nothing,
             so this is usually just `nnroControlObject_`.
  imports  — named symbols this module calls from elsewhere. This is the
             interesting one: it names the API a module depends on.
  modules  — which other modules it imports from.

The header layout follows Citra/Azahar's `cro_helper.h`, which is the
reference implementation.

    python tools/cro_symbols.py <file.cro> [--imports] [--exports] [--modules]

Reads only. Prints symbol names, which are facts about structure rather than
game content -- but the .cro files themselves are copyrighted and must stay out
of the repository.
"""

import argparse
import os
import struct
import sys

CRO_HASH_SIZE = 0x80
MAGIC_CRO0 = 0x304F5243  # "CRO0"
MAGIC_FIXD = 0x44584946  # "FIXD"

# Field order from cro_helper.h. Each is a u32 starting at 0x80.
FIELDS = [
    "Magic", "NameOffset", "NextCRO", "PreviousCRO", "FileSize", "BssSize",
    "FixedSize", "UnknownZero", "UnkSegmentTag", "OnLoadSegmentTag",
    "OnExitSegmentTag", "OnUnresolvedSegmentTag",
    "CodeOffset", "CodeSize", "DataOffset", "DataSize",
    "ModuleNameOffset", "ModuleNameSize", "SegmentTableOffset", "SegmentNum",
    "ExportNamedSymbolTableOffset", "ExportNamedSymbolNum",
    "ExportIndexedSymbolTableOffset", "ExportIndexedSymbolNum",
    "ExportStringsOffset", "ExportStringsSize",
    "ExportTreeTableOffset", "ExportTreeNum",
    "ImportModuleTableOffset", "ImportModuleNum",
    "ExternalRelocationTableOffset", "ExternalRelocationNum",
    "ImportNamedSymbolTableOffset", "ImportNamedSymbolNum",
    "ImportIndexedSymbolTableOffset", "ImportIndexedSymbolNum",
    "ImportAnonymousSymbolTableOffset", "ImportAnonymousSymbolNum",
    "ImportStringsOffset", "ImportStringsSize",
    "StaticAnonymousSymbolTableOffset", "StaticAnonymousSymbolNum",
    "InternalRelocationTableOffset", "InternalRelocationNum",
    "StaticRelocationTableOffset", "StaticRelocationNum",
]
FIELD_INDEX = {name: i for i, name in enumerate(FIELDS)}


class Cro:
    def __init__(self, data):
        self.d = data

    def field(self, name):
        off = CRO_HASH_SIZE + FIELD_INDEX[name] * 4
        return struct.unpack_from("<I", self.d, off)[0]

    def cstring(self, offset, limit):
        """Reads a NUL-terminated string, bounded so a bad offset cannot run
        away through the whole file."""
        if offset >= len(self.d):
            return ""
        end = self.d.find(b"\x00", offset, min(offset + limit, len(self.d)))
        if end < 0:
            end = min(offset + limit, len(self.d))
        return self.d[offset:end].decode("ascii", "replace")

    def module_name(self):
        return self.cstring(self.field("ModuleNameOffset"), self.field("ModuleNameSize") or 64)

    def valid(self):
        return self.field("Magic") in (MAGIC_CRO0, MAGIC_FIXD)

    def named_exports(self):
        """(name, segment_tag) for each exported symbol."""
        base = self.field("ExportNamedSymbolTableOffset")
        count = self.field("ExportNamedSymbolNum")
        strings_size = self.field("ExportStringsSize") or 4096
        out = []
        for i in range(count):
            off = base + i * 8
            if off + 8 > len(self.d):
                break
            name_off, tag = struct.unpack_from("<II", self.d, off)
            out.append((self.cstring(name_off, strings_size), tag))
        return out

    def named_imports(self):
        """(name, relocation_table_offset) for each imported symbol."""
        base = self.field("ImportNamedSymbolTableOffset")
        count = self.field("ImportNamedSymbolNum")
        strings_size = self.field("ImportStringsSize") or 4096
        out = []
        for i in range(count):
            off = base + i * 8
            if off + 8 > len(self.d):
                break
            name_off, reloc = struct.unpack_from("<II", self.d, off)
            out.append((self.cstring(name_off, strings_size), reloc))
        return out

    def import_modules(self):
        """Names of the modules this one imports from."""
        base = self.field("ImportModuleTableOffset")
        count = self.field("ImportModuleNum")
        strings_size = self.field("ImportStringsSize") or 4096
        out = []
        for i in range(count):
            off = base + i * 20      # name_offset + 4 more u32s
            if off + 4 > len(self.d):
                break
            (name_off,) = struct.unpack_from("<I", self.d, off)
            out.append(self.cstring(name_off, strings_size))
        return out


def report(path, want_imports, want_exports, want_modules):
    with open(path, "rb") as f:
        cro = Cro(f.read())

    if not cro.valid():
        print(f"{os.path.basename(path)}: not a CRO (magic {cro.field('Magic'):#010x})")
        return 1

    name = cro.module_name() or os.path.basename(path)
    print(f"=== {name}  ({len(cro.d)} bytes) ===")
    print(f"  code {cro.field('CodeSize'):>8} bytes at {cro.field('CodeOffset'):#08x}")
    print(f"  data {cro.field('DataSize'):>8} bytes at {cro.field('DataOffset'):#08x}")
    print(f"  exports {cro.field('ExportNamedSymbolNum')},"
          f" imports {cro.field('ImportNamedSymbolNum')},"
          f" import modules {cro.field('ImportModuleNum')}")

    if want_modules:
        mods = cro.import_modules()
        print(f"\n  -- imports from {len(mods)} modules --")
        for m in mods:
            print(f"     {m}")

    if want_exports:
        exps = cro.named_exports()
        print(f"\n  -- {len(exps)} named exports --")
        for n, tag in exps:
            print(f"     {n}  (tag {tag:#010x})")

    if want_imports:
        imps = cro.named_imports()
        print(f"\n  -- {len(imps)} named imports --")
        for n, _ in imps:
            print(f"     {n}")

    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--imports", action="store_true")
    ap.add_argument("--exports", action="store_true")
    ap.add_argument("--modules", action="store_true")
    ap.add_argument("--summary", action="store_true",
                    help="one line per file: name, sizes, symbol counts")
    args = ap.parse_args()

    if args.summary:
        print(f"{'module':<32} {'code':>9} {'data':>9} {'exp':>5} {'imp':>6} {'mods':>5}")
        for path in args.files:
            try:
                with open(path, "rb") as f:
                    cro = Cro(f.read())
                if not cro.valid():
                    continue
                print(f"{cro.module_name() or os.path.basename(path):<32}"
                      f" {cro.field('CodeSize'):>9}"
                      f" {cro.field('DataSize'):>9}"
                      f" {cro.field('ExportNamedSymbolNum'):>5}"
                      f" {cro.field('ImportNamedSymbolNum'):>6}"
                      f" {cro.field('ImportModuleNum'):>5}")
            except OSError as e:
                print(f"{path}: {e}", file=sys.stderr)
        raise SystemExit(0)

    if not (args.imports or args.exports or args.modules):
        args.imports = args.exports = args.modules = True

    rc = 0
    for path in args.files:
        rc |= report(path, args.imports, args.exports, args.modules)
    raise SystemExit(rc)
