/*
 * XYORAS Access — reading gfl::str::StrBuf, GameFreak's string object.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Layout recovered from the constructor at 0x0038E4A8 in a decompiled code.bin
 * (see tools/romextract and AI docks/12-research-log.md):
 *
 *     +0x00  vtable            always 0x00598570
 *     +0x04  u32   buffer      pointer to UTF-16 characters
 *     +0x08  u16   capacity    length + 1, the NUL included
 *     +0x0A  u16   length      characters, NOT bytes
 *     +0x0C  u8    owns        set once the copy succeeded
 *
 * The constructor allocates capacity << 1 bytes, which is what settles the
 * encoding: two bytes per character, UTF-16, same as a TextBox pane.
 *
 * Header-only and 3DS-free so the host tests exercise it.
 */
#ifndef XYORAS_STRBUF_HPP
#define XYORAS_STRBUF_HPP

#include "xyoras/common.hpp"
#include "xyoras/memchain.hpp"
#include "xyoras/textbox.hpp"

#include <string>

namespace xyoras { namespace strbuf {

    constexpr u32 kVtable   = 0x00598570;
    constexpr u32 kBufferPtr = 0x04;
    constexpr u32 kCapacity  = 0x08;
    constexpr u32 kLength    = 0x0A;
    constexpr u32 kOwnsFlag  = 0x0C;

    /// Longest string worth believing. A wrong address yields a garbage length,
    /// and reading megabytes of heap because of it would hang the poll thread.
    constexpr u16 kMaxChars = 2048;

    /// Reads the text out of a StrBuf at `object`.
    ///
    /// Returns false when the object holds nothing worth saying -- an empty
    /// buffer, an implausible length, or an unreadable pointer. Empty is the
    /// common case: these are pooled, and most are idle at any moment.
    inline bool ReadString(mem::Read32Fn read32, textbox::Read16Fn read16, void *ctx,
                           u32 object, std::string &out)
    {
        out.clear();

        if (read32 == nullptr || read16 == nullptr || !mem::InHeap(object))
            return false;

        u32 buffer = 0;
        if (!read32(object + kBufferPtr, buffer, ctx) || !mem::InHeap(buffer))
            return false;

        u32 packed = 0;
        if (!read32(object + kCapacity, packed, ctx))
            return false;

        // capacity is the low half, length the high half of the same word
        const u16 capacity = static_cast<u16>(packed & 0xFFFF);
        const u16 length   = static_cast<u16>(packed >> 16);

        if (length == 0 || length > kMaxChars || capacity <= length)
            return false;

        bool inCommand = false;
        for (u16 i = 0; i < length; ++i)
        {
            u16 ch = 0;
            if (!read16(buffer + i * 2, ch, ctx))
                return false;
            if (ch == 0)
                break;
            textbox::AppendFolded(ch, inCommand, out);
        }

        textbox::CollapseSpaces(out);
        return !out.empty();
    }

}}   // namespace xyoras::strbuf

#endif
