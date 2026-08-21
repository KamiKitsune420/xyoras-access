/*
 * XYORAS Access — finding live C++ objects by their vtable.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Gen 6 ships with RTTI intact, which lets a class name be followed back to
 * its vtable address (see `tools/find_vtables.py` and
 * "AI docks/04-gen6-reverse-engineering.md"). Those addresses are constant,
 * because the classes we care about live in `code.bin`, which loads at a fixed
 * base.
 *
 * That turns object discovery into a search. Every instance of a polymorphic
 * class begins with a pointer to its vtable, so scanning the heap for that
 * exact value finds the live objects -- no pointer chain to map by hand, and
 * no disassembly.
 *
 * It also self-verifies. If a scan for `app::tool::TalkWindow` returns hits
 * only while a message box is on screen, both the vtable address and the
 * identification are confirmed at once.
 *
 * The reader is injected, exactly as in memchain.hpp, so the host tests drive
 * this against a fake address space.
 */
#ifndef XYORAS_VTSCAN_HPP
#define XYORAS_VTSCAN_HPP

#include "xyoras/common.hpp"
#include "xyoras/memchain.hpp"

namespace xyoras { namespace vtscan {

    /// Where `code.bin` is mapped. Everything reachable by a fixed address
    /// lives between here and roughly +0x4EB000 for Pokemon X.
    constexpr u32 kCodeBase = 0x00100000;
    constexpr u32 kCodeMax  = 0x00700000;   ///< generous; CROs load above this

    /// A vtable address must at least lie inside the executable. Rejecting
    /// anything else stops a typo in the address table turning into a scan
    /// that matches arbitrary heap data.
    inline bool PlausibleVtable(u32 address)
    {
        return address >= kCodeBase && address < kCodeMax && (address & 3) == 0;
    }

    /// Scans [start, end) for words equal to `vtable`, recording the address
    /// of each match -- that is, the address of the object itself, since the
    /// vtable pointer is its first member.
    ///
    /// Returns the number of matches found, which may exceed `maxHits`; only
    /// the first `maxHits` are written to `hits`. Reporting the true total
    /// matters, because "exactly one instance" and "hundreds" mean very
    /// different things about whether the address is right.
    inline u32 FindObjects(mem::Read32Fn read, void *ctx,
                           u32 start, u32 end, u32 vtable,
                           u32 *hits, u32 maxHits)
    {
        if (read == nullptr || !PlausibleVtable(vtable))
            return 0;
        if (start >= end || (start & 3) != 0)
            return 0;

        u32 total = 0;
        for (u32 addr = start; addr + 4 <= end; addr += 4)
        {
            u32 value = 0;
            if (!read(addr, value, ctx))
                continue;       // unmapped page: skip, do not abort the scan

            if (value != vtable)
                continue;

            if (hits != nullptr && total < maxHits)
                hits[total] = addr;
            ++total;
        }
        return total;
    }

    /// Convenience wrapper over the whole game heap.
    inline u32 FindObjectsInHeap(mem::Read32Fn read, void *ctx, u32 vtable,
                                 u32 *hits, u32 maxHits)
    {
        return FindObjects(read, ctx, mem::kHeapMin, mem::kHeapMax,
                           vtable, hits, maxHits);
    }

    /// Reads the vtable pointer of a candidate object.
    ///
    /// Useful in the other direction: given an address believed to hold an
    /// object, ask what it actually is. An address whose first word is a
    /// plausible vtable is very likely a live polymorphic object; anything
    /// else is not.
    inline bool ReadVtable(mem::Read32Fn read, void *ctx, u32 object, u32 &out)
    {
        if (read == nullptr || !mem::InHeap(object))
            return false;

        u32 value = 0;
        if (!read(object, value, ctx))
            return false;
        if (!PlausibleVtable(value))
            return false;

        out = value;
        return true;
    }

}} // namespace xyoras::vtscan

#endif
