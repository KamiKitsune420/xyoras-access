/*
 * XYORAS Access — reading app::tool::MenuWindow, the game's menu owner.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Layout recovered from the constructor at 0x00331200 in a decompiled code.bin:
 *
 *     +0x00  vtable          always 0x005970E0
 *     +0x14  u32  dimA       \  item count is the product; a menu is laid out
 *     +0x18  u32  dimB       /  as a grid even when it is a single column
 *     +0x20  u32  items      pointer to the entry array, stride 0x1C
 *     +0x28  u32  selected   index of the focused entry, 0xFFFFFFFF for none
 *
 * The constructor allocates dimA*dimB entries of 0x1C bytes and gives each one
 * a 0x40-byte block at entry+0x14.
 *
 * Confirmed against a live two-option YES/NO menu: dimA=2, dimB=1, selected=1.
 *
 * Why this matters: reading every option aloud whenever a menu opens is the
 * flood the whole narration design exists to avoid, and it is what the mod did
 * before. Knowing which entry is focused is what lets it behave like a screen
 * reader instead -- announce the menu once, then one option per cursor move.
 *
 * Header-only and 3DS-free so the host tests exercise it.
 */
#ifndef XYORAS_MENUWINDOW_HPP
#define XYORAS_MENUWINDOW_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace menuwindow {

    constexpr u32 kVtable    = 0x005970E0;
    constexpr u32 kDimA      = 0x14;
    constexpr u32 kDimB      = 0x18;
    constexpr u32 kItemArray = 0x20;
    constexpr u32 kSelected  = 0x28;

    constexpr u32 kEntryStride  = 0x1C;
    constexpr u32 kEntryPayload = 0x14;   ///< the 0x40-byte block per entry

    /// Nothing focused. The constructor writes this, so it is seen whenever a
    /// menu exists but the cursor has not landed yet.
    constexpr u32 kNoSelection = 0xFFFFFFFFu;

    /// More entries than any real menu. A wrong address gives a wild product,
    /// and trusting it would walk the heap.
    constexpr u32 kMaxItems = 64;

    struct State
    {
        u32 count;
        u32 selected;
        u32 items;

        State() : count(0), selected(kNoSelection), items(0) {}

        bool HasFocus() const
        {
            return count > 0 && selected < count;
        }
    };

    /// Reads a menu's shape and cursor position. False if the object does not
    /// look like a menu, which is the normal answer for a stale address.
    inline bool Read(mem::Read32Fn read32, void *ctx, u32 object, State &out)
    {
        out = State();

        if (read32 == nullptr || !mem::InHeap(object))
            return false;

        u32 dimA = 0, dimB = 0, items = 0, selected = 0;
        if (!read32(object + kDimA, dimA, ctx) ||
            !read32(object + kDimB, dimB, ctx) ||
            !read32(object + kItemArray, items, ctx) ||
            !read32(object + kSelected, selected, ctx))
            return false;

        if (dimA == 0 || dimB == 0)
            return false;
        if (dimA > kMaxItems || dimB > kMaxItems)
            return false;

        const u32 count = dimA * dimB;
        if (count == 0 || count > kMaxItems || !mem::InHeap(items))
            return false;

        out.count = count;
        out.selected = selected;
        out.items = items;
        return true;
    }

    /// Address of the per-entry payload block for `index`, or 0 if out of range.
    inline u32 EntryPayload(const State &s, u32 index)
    {
        if (index >= s.count || s.items == 0)
            return 0;
        return s.items + index * kEntryStride + kEntryPayload;
    }

}}   // namespace xyoras::menuwindow

#endif
