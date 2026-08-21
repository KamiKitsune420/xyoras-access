/*
 * XYORAS Access — reading on-screen text out of the game.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Every piece of text Gen 6 draws -- dialogue, menus, labels, prompts -- goes
 * through `nw::lyt::TextBox`, the NintendoWare layout text pane. Each instance
 * keeps a pointer to its UTF-16 string at +0xD4.
 *
 * That single fact replaces the render hook this project spent a long time
 * looking for. There is no need to intercept a drawing call: find the objects,
 * read their strings, and notice when they change. One mechanism covers every
 * subsystem, because they all draw through the same class.
 *
 * How the offset was established, and the evidence for it, is in
 * "AI docks/12-research-log.md". Verified by reading real text out of a
 * running game.
 *
 * The reader is injected as everywhere else, so the host tests drive this
 * against a fake address space.
 */
#ifndef XYORAS_TEXTBOX_HPP
#define XYORAS_TEXTBOX_HPP

#include "xyoras/common.hpp"
#include "xyoras/memchain.hpp"

#include <string>

namespace xyoras { namespace textbox {

    /// Offset of the string pointer within an nw::lyt::TextBox.
    ///
    /// TextBox derives from nw::lyt::Pane, whose name, transform, size and
    /// child links fill everything before this -- which is why a search over
    /// the first 0xA0 bytes of the object finds nothing.
    constexpr u32 kStringOffset = 0xD4;

    /// Longest string we will read. Game text runs to a couple of hundred
    /// characters; far beyond that means the pointer is not really a string
    /// and we are walking through unrelated memory.
    constexpr u32 kMaxChars = 512;

    /// Reads a u16. Layered on the same injected reader as everything else so
    /// there is one place that touches game memory.
    typedef bool (*Read16Fn)(u32 address, u16 &out, void *ctx);

    /// Reads the UTF-16 string a TextBox points at.
    ///
    /// Returns false when the object holds no string, which is normal: plenty
    /// of panes are laid out but empty.
    ///
    /// Non-ASCII becomes '?'. eSpeak is fed ASCII, and a stray high codepoint
    /// is spoken as noise rather than skipped. Proper transcoding arrives with
    /// multi-language support.
    inline bool ReadString(mem::Read32Fn read32, Read16Fn read16, void *ctx,
                           u32 object, std::string &out)
    {
        out.clear();

        if (read32 == nullptr || read16 == nullptr || !mem::InHeap(object))
            return false;

        u32 textPtr = 0;
        if (!read32(object + kStringOffset, textPtr, ctx))
            return false;

        // The string lives on the heap like everything else. A pointer outside
        // it means this object is not what we think it is.
        if (!mem::InHeap(textPtr))
            return false;

        out.reserve(64);
        for (u32 i = 0; i < kMaxChars; ++i)
        {
            u16 ch = 0;
            if (!read16(textPtr + i * 2, ch, ctx))
                return false;       // ran off a mapped page mid-string

            if (ch == 0x0000)
                break;              // terminator

            // The games use 0xFFFF as a terminator in some structures, and it
            // is never a real character.
            if (ch == 0xFFFF)
                break;

            if (ch == 0x000A || ch == 0x000D)
            {
                // Line breaks are layout, not speech. A space keeps words from
                // running together without inventing a pause.
                out.push_back(' ');
                continue;
            }

            out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
        }

        return !out.empty();
    }

    /// True if a string looks like something worth speaking.
    ///
    /// Layout panes hold plenty of text that is not language: separator rows
    /// of dashes, single glyphs used as icons, padding. Speaking those is
    /// worse than silence, because the player has to listen through them.
    inline bool WorthSpeaking(const std::string &s)
    {
        if (s.size() < 2)
            return false;

        u32 letters = 0;
        u32 unknown = 0;
        for (u32 i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                ++letters;
            else if (c == '?')
                ++unknown;
        }

        // At least a couple of real letters, and not mostly unreadable.
        if (letters < 2)
            return false;
        if (unknown > s.size() / 2)
            return false;

        return true;
    }

}} // namespace xyoras::textbox

#endif
