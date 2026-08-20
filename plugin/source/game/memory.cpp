/*
 * XYORAS Access — guarded game memory access.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Every read of game memory in this project goes through here. Nothing
 * dereferences a game pointer directly: a fault inside a plugin takes the
 * game down with it, and a blind player cannot read the exception screen.
 */
#include "xyoras/game.hpp"

namespace xyoras { namespace game {

using CTRPluginFramework::Process;

bool InHeap(u32 address)
{
    return address >= kHeapMin && address < kHeapMax;
}

namespace {
    /// Common precondition for every read: plausible address, and the process
    /// agrees the page is actually readable.
    inline bool Readable(u32 address, u32 size)
    {
        if (!InHeap(address))
            return false;
        if (!InHeap(address + size - 1))   // reject reads that run off the end
            return false;
        return Process::CheckAddress(address, MEMPERM_READ);
    }
}

bool Read8(u32 address, u8 &out)
{
    if (!Readable(address, sizeof(u8)))
        return false;
    return Process::Read8(address, out);
}

bool Read16(u32 address, u16 &out)
{
    if (!Readable(address, sizeof(u16)))
        return false;
    return Process::Read16(address, out);
}

bool Read32(u32 address, u32 &out)
{
    if (!Readable(address, sizeof(u32)))
        return false;
    return Process::Read32(address, out);
}

bool Read64(u32 address, u64 &out)
{
    if (!Readable(address, sizeof(u64)))
        return false;
    return Process::Read64(address, out);
}

bool ReadBuf(u32 address, void *out, u32 size)
{
    if (out == nullptr || size == 0)
        return false;
    if (!Readable(address, size))
        return false;
    return Process::CopyMemory(out, reinterpret_cast<void *>(address), size);
}

bool ReadPtr(u32 address, u32 &out)
{
    u32 value = 0;
    if (!Read32(address, value))
        return false;
    if (!InHeap(value))     // a pointer that lands outside the heap is garbage
        return false;
    out = value;
    return true;
}

bool ReadChain(u32 base, const u32 *offsets, u32 count, u32 &out)
{
    if (offsets == nullptr)
        return false;

    u32 cursor = base;
    for (u32 i = 0; i < count; ++i)
    {
        u32 next = 0;
        if (!ReadPtr(cursor, next))
            return false;
        cursor = next + offsets[i];
    }

    out = cursor;
    return InHeap(out);
}

bool ReadUtf16(u32 address, u32 maxChars, std::string &out)
{
    out.clear();

    if (maxChars == 0 || !Readable(address, maxChars * sizeof(u16)))
        return false;

    out.reserve(maxChars);

    for (u32 i = 0; i < maxChars; ++i)
    {
        u16 unit = 0;
        if (!Process::Read16(address + i * sizeof(u16), unit))
            return false;
        if (unit == 0x0000 || unit == 0xFFFF)   // 0xFFFF is the games' terminator
            break;

        // Names in these games are overwhelmingly Latin-1 in an English save.
        // Anything else becomes '?' rather than corrupting the utterance;
        // proper UTF-8 transcoding lands with multi-language support.
        out.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    }

    return true;
}

}} // namespace xyoras::game
