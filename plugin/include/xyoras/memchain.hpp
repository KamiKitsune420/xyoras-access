/*
 * XYORAS Access — pointer-chain walking and range guards.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Gen 6 allocates from heaps, so almost nothing interesting sits at a fixed
 * address. Reaching it means following a chain of pointers, and every link is
 * an opportunity to fault: a stale pointer, a structure the game freed, a read
 * during a load screen. A fault inside a plugin takes the game down with it,
 * and a blind player cannot read the exception screen.
 *
 * So the walking policy lives here, separated from the mechanism that actually
 * touches memory. The reader is injected, which lets the host tests drive the
 * exact same code with a fake address space -- including the failure cases that
 * are impossible to provoke on demand on real hardware.
 */
#ifndef XYORAS_MEMCHAIN_HPP
#define XYORAS_MEMCHAIN_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace mem {

    /// Heap addresses observed in Gen 6. Anything outside this window is not
    /// data -- it is a bad pointer that has not faulted yet.
    constexpr u32 kHeapMin = 0x08000000;
    constexpr u32 kHeapMax = 0x08DF0000;

    inline bool InHeap(u32 address)
    {
        return address >= kHeapMin && address < kHeapMax;
    }

    /// True if a whole object of `size` bytes starting at `address` fits in the
    /// heap window. Checked with subtraction so a size near 2^32 cannot wrap
    /// the sum and sneak past.
    inline bool InHeapRange(u32 address, u32 size)
    {
        if (size == 0 || !InHeap(address))
            return false;
        return size <= (kHeapMax - address);
    }

    /// Reads 4 bytes. Returns false if the address is unreadable.
    /// `ctx` is passed through untouched, so the caller can carry whatever
    /// state the real reader needs.
    typedef bool (*Read32Fn)(u32 address, u32 &out, void *ctx);

    /// Reads a pointer and rejects it unless it lands in the heap.
    inline bool ReadPointer(Read32Fn read, void *ctx, u32 address, u32 &out)
    {
        if (read == nullptr || !InHeap(address))
            return false;

        u32 value = 0;
        if (!read(address, value, ctx))
            return false;
        if (!InHeap(value))
            return false;

        out = value;
        return true;
    }

    /// Follows a chain: dereference, add offset, repeat.
    ///
    ///     WalkChain(base, {0x10, 0x24}) == *( *(base) + 0x10 ) + 0x24
    ///
    /// Every intermediate pointer is range-checked, so a chain that goes bad
    /// halfway fails cleanly instead of reading from a wild address. With
    /// `count == 0` this validates `base` and returns it unchanged.
    inline bool WalkChain(Read32Fn read, void *ctx, u32 base,
                          const u32 *offsets, u32 count, u32 &out)
    {
        // Reject a null reader even when count == 0 and it would never be
        // called. Passing one is a programming error, and a guard that
        // sometimes tolerates it hides the mistake until the day a caller
        // passes a non-zero count.
        if (read == nullptr)
            return false;
        if (count > 0 && offsets == nullptr)
            return false;
        if (!InHeap(base))
            return false;

        u32 cursor = base;
        for (u32 i = 0; i < count; ++i)
        {
            u32 next = 0;
            if (!ReadPointer(read, ctx, cursor, next))
                return false;

            // The offset must not carry the cursor past the end of the address
            // space; checking before adding keeps the result meaningful.
            if (offsets[i] > (0xFFFFFFFFu - next))
                return false;

            cursor = next + offsets[i];
            if (!InHeap(cursor))
                return false;
        }

        out = cursor;
        return true;
    }

}} // namespace xyoras::mem

#endif
