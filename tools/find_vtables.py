#!/usr/bin/env python3
"""Locate C++ vtables in a 3DS executable by following RTTI back to them.

Gen 6 ships with RTTI intact, which is a large gift. Every polymorphic class
leaves a type-name string in the binary, and the Itanium/ARM C++ ABI chains
those together in a fixed way:

    vtable:  [ offset-to-top ][ typeinfo* ][ virtual fn ][ virtual fn ] ...
    typeinfo:              [ vtable-of-typeinfo* ][ name* ][ ... ]

So the search runs backwards. Find the name string, find the pointer to it
(that is inside the typeinfo), then find the pointer to the typeinfo (that is
inside the vtable, one word before the first virtual function).

Why it matters here: a vtable address identifies objects of that class at
runtime. Scanning memory for a pointer to `app::tool::TalkWindow`'s vtable
finds the live dialogue box, which is otherwise buried behind pointer chains
nobody has mapped.

    python tools/find_vtables.py <code.bin> gfl::str::StrBuf app::tool::TalkWindow

Reads only, and prints addresses rather than any game content. The executable
itself is copyrighted and must stay out of the repository.
"""

import argparse
import struct

# Where the 3DS maps a title's .text. The dump starts at the beginning of
# .text, so file offset 0 corresponds to this address.
DEFAULT_BASE = 0x00100000


def mangle(name):
    """gfl::str::StrBuf -> N3gfl3str6StrBufE, the RTTI form."""
    parts = name.split("::")
    if len(parts) == 1:
        return f"{len(parts[0])}{parts[0]}"
    return "N" + "".join(f"{len(p)}{p}" for p in parts) + "E"


def find_all(data, needle, start=0):
    out = []
    i = data.find(needle, start)
    while i >= 0:
        out.append(i)
        i = data.find(needle, i + 1)
    return out


# ARM function entries almost always begin by saving registers. These are the
# common encodings; the list is deliberately narrow, so a "partial" score means
# the detector did not recognise a prologue, not that the vtable is wrong.
def prologue_kind(word):
    if (word & 0xFFFF0000) == 0xE92D0000:      # stmfd sp!, {..} / push
        return "push"
    if (word & 0x0FFF0000) == 0x092D0000:      # conditional push
        return "push?"
    if (word & 0xFFFFF000) == 0xE24DD000:      # sub sp, sp, #n
        return "sub sp"
    if (word & 0xFFF00000) == 0xE5200000:      # str rX, [sp, #-n]!
        return "str!"
    return None


def verify_vtable(data, base, vt_va, entries=8):
    """Do this vtable's entries point at real ARM functions?

    A genuine vtable is followed by function pointers, and those functions
    begin with a recognisable prologue. Checking that distinguishes a real
    vtable from an accidental match on a typeinfo pointer.
    """
    off = vt_va - base
    good = total = 0
    samples = []
    for i in range(1, entries + 1):
        p = off + i * 4
        if p + 4 > len(data):
            break
        (fn,) = struct.unpack_from("<I", data, p)
        if fn == 0 or not (base <= fn < base + len(data)):
            continue
        total += 1
        (insn,) = struct.unpack_from("<I", data, fn - base)
        kind = prologue_kind(insn)
        if kind:
            good += 1
            if len(samples) < 2:
                samples.append(f"{fn:08X}:{insn:08X}({kind})")
    return good, total, samples


def analyse(data, base, name):
    mangled = mangle(name)
    target = mangled.encode() + b"\x00"

    print(f"\n=== {name} ===")
    print(f"  RTTI symbol : {mangled}")

    string_offsets = find_all(data, target)
    if not string_offsets:
        print("  name string : NOT FOUND")
        return

    for so in string_offsets:
        sva = base + so
        print(f"  name string : file +0x{so:06X}  va 0x{sva:08X}")

        # A pointer to the name lives inside the typeinfo object.
        refs = find_all(data, struct.pack("<I", sva))
        if not refs:
            print("    no pointer to the name found (typeinfo may be elsewhere)")
            continue

        for r in refs:
            # In the ARM C++ ABI the name pointer is the second word of
            # typeinfo, so the object starts one word earlier.
            ti_off = r - 4
            if ti_off < 0:
                continue
            ti_va = base + ti_off
            print(f"    typeinfo  : file +0x{ti_off:06X}  va 0x{ti_va:08X}")

            # A pointer to the typeinfo sits in the vtable, immediately before
            # the first virtual function.
            vt_refs = find_all(data, struct.pack("<I", ti_va))
            for v in vt_refs:
                if v == r:
                    continue
                vt_va = base + v
                # Virtual functions follow the typeinfo pointer. Show a few so
                # the result can be sanity-checked: they should look like code
                # addresses in the same region.
                fns = []
                for k in range(1, 5):
                    off = v + k * 4
                    if off + 4 <= len(data):
                        (fn,) = struct.unpack_from("<I", data, off)
                        fns.append(fn)
                plausible = sum(1 for f in fns if base <= f < base + len(data))
                verdict = "vtable" if plausible >= 3 else "reference"
                line = (f"      {verdict:<9} : va 0x{vt_va:08X}"
                        f"   first fns " + " ".join(f"{f:08X}" for f in fns[:3]))
                if verdict == "vtable":
                    good, total, samples = verify_vtable(data, base, vt_va)
                    line += (f"\n        prologues {good}/{total}  "
                             + " ".join(samples))
                print(line)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("names", nargs="+")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=DEFAULT_BASE)
    args = ap.parse_args()

    with open(args.binary, "rb") as f:
        data = f.read()

    print(f"binary {len(data)} bytes, assumed load base 0x{args.base:08X}")
    for n in args.names:
        analyse(data, args.base, n)
