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

    // -------------------------------------------------------------------------
    // Blockwise scanning
    //
    // FindObjects above reads one word at a time, and on real hardware every
    // one of those goes through a range check and a permission query before it
    // touches anything. Over the whole heap that is 3.6 million of them, which
    // is not a scan so much as a stall.
    //
    // Reading a page at a time and searching it locally does the same work with
    // one check per page instead of one per word -- about 880 checks rather
    // than 3,600,000. The word-at-a-time version stays because it is the
    // simplest thing that can be pointed at an arbitrary range.
    // -------------------------------------------------------------------------

    /// Copies `size` bytes into `out`. Returns false if any of it is
    /// unreadable, in which case the whole block is skipped -- which is why
    /// blocks must not straddle a page boundary.
    typedef bool (*ReadBlockFn)(u32 address, void *out, u32 size, void *ctx);

    /// The 3DS page size. A block that stayed inside one page can only fail as
    /// a whole, so nothing readable is ever lost by skipping it.
    constexpr u32 kPageSize = 0x1000;

    /// Scans [start, end) a block at a time, using a caller-supplied buffer.
    ///
    /// The buffer comes from the caller because this runs inside a game
    /// process where allocation is worth avoiding, and because the host tests
    /// then exercise the same code with no allocator at all.
    ///
    /// `bufferWords` is clamped to a page. `start` must be block-aligned;
    /// otherwise a block could span two pages and one unmapped neighbour would
    /// discard everything mapped beside it.
    ///
    /// Returns the true number of matches, as FindObjects does.
    inline u32 FindObjectsBlockwise(ReadBlockFn readBlock, void *ctx,
                                    u32 start, u32 end, u32 vtable,
                                    u32 *hits, u32 maxHits,
                                    u32 *buffer, u32 bufferWords)
    {
        if (readBlock == nullptr || buffer == nullptr || bufferWords == 0)
            return 0;
        if (!PlausibleVtable(vtable))
            return 0;
        if (start >= end || (start & 3) != 0)
            return 0;

        // Clamp to a page, then down to a power of two, so a block can never
        // straddle a page boundary.
        u32 words = bufferWords;
        if (words > kPageSize / 4)
            words = kPageSize / 4;

        u32 pow2 = 1;
        while (pow2 * 2 <= words)
            pow2 *= 2;
        words = pow2;

        const u32 blockBytes = words * 4;
        if ((start & (blockBytes - 1)) != 0)
            return 0;

        u32 total = 0;
        for (u32 base = start; base < end; base += blockBytes)
        {
            // The last block may run past `end`; read only as far as asked.
            u32 wantBytes = blockBytes;
            if (end - base < wantBytes)
                wantBytes = (end - base) & ~3u;
            if (wantBytes == 0)
                break;

            if (!readBlock(base, buffer, wantBytes, ctx))
                continue;       // unmapped: skip the block, not the scan

            const u32 wantWords = wantBytes / 4;
            for (u32 i = 0; i < wantWords; ++i)
            {
                if (buffer[i] != vtable)
                    continue;

                if (hits != nullptr && total < maxHits)
                    hits[total] = base + i * 4;
                ++total;
            }
        }
        return total;
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
