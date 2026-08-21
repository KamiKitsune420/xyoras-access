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

    /// True for a code unit eSpeak can be handed directly.
    inline bool IsPlainAscii(u16 ch)
    {
        return ch >= 0x20 && ch < 0x7F;
    }

    /// Folds one UTF-16 code unit into ASCII, appending nothing if it should
    /// be dropped.
    ///
    /// Three kinds of thing arrive here that are not letters:
    ///
    /// - **The game's own inline text commands.** Real text read out of
    ///   Pokemon X contains runs like `0x10 0x02 0x00E9 0x01` in the middle of
    ///   a sentence -- markers the engine uses for pauses, colour changes and
    ///   substitutions. See `inCommand` below for how they are skipped.
    /// - **Typographic punctuation.** Curly quotes, dashes and ellipses are
    ///   everywhere in this game's script. Folded to their ASCII equivalents.
    /// - **Accented letters.** Place and species names carry them. Folded to
    ///   the base letter, which eSpeak pronounces close enough, rather than
    ///   dropped -- "Hungria" is far better than "Hungra".
    ///
    /// Anything left over is dropped rather than replaced with '?'. A '?' is
    /// not silent: eSpeak reads it as a question, changing the intonation of a
    /// sentence that was never a question.
    ///
    /// `inCommand` carries the one piece of state this needs across units. A
    /// control code starts a command, and everything after it that is not
    /// plain ASCII is taken to be its parameters and dropped; the first plain
    /// ASCII character ends it. That rule was chosen because it fits what the
    /// game actually produced -- the `0x00E9` above is a parameter, and
    /// folding it to "e" put a stray letter into the middle of a sentence --
    /// while being unable to swallow real text, which is always plain ASCII in
    /// an English script. It is a heuristic, not a decoder: the command format
    /// is not documented here, and decoding it properly is the real fix.
    inline void AppendFolded(u16 ch, bool &inCommand, std::string &out)
    {
        // Line breaks are layout, not speech. A space keeps words from running
        // together without inventing a pause. Runs are collapsed later.
        if (ch == 0x000A || ch == 0x000D || ch == 0x0009)
        {
            out.push_back(' ');
            return;
        }

        if (ch < 0x20)
        {
            inCommand = true;   // a command marker, and its parameters follow
            return;
        }

        if (inCommand)
        {
            if (!IsPlainAscii(ch))
                return;         // still inside the command's parameters
            inCommand = false;  // real text resumes
        }

        if (ch < 0x80)
        {
            out.push_back(static_cast<char>(ch));
            return;
        }

        switch (ch)
        {
            case 0x00A0: out.push_back(' ');  return;   // non-breaking space
            case 0x2018:                                 // left single quote
            case 0x2019: out.push_back('\''); return;   // right single quote
            case 0x201C:                                 // left double quote
            case 0x201D: out.push_back('"');  return;   // right double quote
            case 0x2010:
            case 0x2011:
            case 0x2012:
            case 0x2013:                                 // en dash
            case 0x2014: out.push_back('-');  return;   // em dash
            case 0x2026: out += "...";        return;   // ellipsis
            case 0x00D7: out.push_back('x');  return;   // multiplication sign
            default: break;
        }

        // Latin-1 letters, folded to their base. The table covers 0xC0-0xFF,
        // which is every accented letter these games use in a Western script.
        if (ch >= 0x00C0 && ch <= 0x00FF)
        {
            static const char kLatin1[] =
                "AAAAAAACEEEEIIII"      // C0-CF
                "DNOOOOO.OUUUUY.s"      // D0-DF  (0xD7 handled above, 0xDF -> s)
                "aaaaaaaceeeeiiii"      // E0-EF
                "dnooooo.ouuuuy.y";     // F0-FF
            const char folded = kLatin1[ch - 0x00C0];
            if (folded != '.')
                out.push_back(folded);
            return;
        }

        // Everything else -- button glyphs, other scripts -- is dropped.
        // Saying nothing is better than saying the wrong thing.
    }

    /// Collapses runs of spaces and trims the ends.
    ///
    /// Layout text is full of padding: the language prompt read out of a
    /// running game began with two spaces and had a double space mid-sentence
    /// where a command had been stripped. eSpeak does not care about extra
    /// spaces, but the trace log and any text comparison do -- and a string
    /// that is nothing but padding must come out empty so it is never spoken.
    inline void CollapseSpaces(std::string &s)
    {
        std::string out;
        out.reserve(s.size());

        bool pendingSpace = false;
        for (u32 i = 0; i < s.size(); ++i)
        {
            if (s[i] == ' ')
            {
                pendingSpace = !out.empty();   // never leading
                continue;
            }
            if (pendingSpace)
            {
                out.push_back(' ');
                pendingSpace = false;
            }
            out.push_back(s[i]);
        }

        s.swap(out);
    }

    /// Reads the UTF-16 string a TextBox points at.
    ///
    /// Returns false when the object holds no string, which is normal: plenty
    /// of panes are laid out but empty. Also false when everything in it was
    /// dropped as non-speech, which is the same thing as far as callers care.
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
        bool inCommand = false;
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

            AppendFolded(ch, inCommand, out);
        }

        CollapseSpaces(out);
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
