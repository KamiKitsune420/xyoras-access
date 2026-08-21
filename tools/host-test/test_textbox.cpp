/*
 * XYORAS Access — host tests for reading on-screen text.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * These pin the reading of nw::lyt::TextBox strings, using the same fixtures
 * the real game produced: "Your name?", "Delete", "SAVE" and the separator row
 * of dashes that must NOT be spoken.
 *
 * The failure that matters is not crashing -- it is speaking rubbish. A layout
 * is full of panes holding dashes, single glyphs and padding, and reading those
 * aloud is worse than silence because the player has to sit through them.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/textbox.hpp"

#include <map>
#include <string>

using namespace xyoras;

namespace {

    struct FakeMemory
    {
        std::map<u32, u32> words;
        std::map<u32, u16> halves;

        void PutWord(u32 a, u32 v) { words[a] = v; }

        /// Lays out a UTF-16 string, NUL-terminated, as the game would.
        void PutString(u32 a, const std::string &ascii)
        {
            for (u32 i = 0; i < ascii.size(); ++i)
                halves[a + i * 2] = static_cast<u16>(ascii[i]);
            halves[a + static_cast<u32>(ascii.size()) * 2] = 0;
        }

        /// Builds a TextBox whose string pointer points at `textAddr`.
        void PutTextBox(u32 object, u32 textAddr)
        {
            words[object + textbox::kStringOffset] = textAddr;
        }
    };

    bool Read32(u32 a, u32 &out, void *ctx)
    {
        FakeMemory *m = static_cast<FakeMemory *>(ctx);
        std::map<u32, u32>::const_iterator it = m->words.find(a);
        if (it == m->words.end())
            return false;
        out = it->second;
        return true;
    }

    bool Read16(u32 a, u16 &out, void *ctx)
    {
        FakeMemory *m = static_cast<FakeMemory *>(ctx);
        std::map<u32, u16>::const_iterator it = m->halves.find(a);
        if (it == m->halves.end())
            return false;
        out = it->second;
        return true;
    }

    const u32 kObject = 0x08210348;
    const u32 kText   = 0x08210500;

    void TestReadsRealStrings(void)
    {
        test::Section("strings the game actually produced");

        // Every one of these was read out of a running Pokemon X.
        const char *samples[] = {"Your name?", "Delete", "Space", "SAVE", "OPTIONS"};

        for (u32 i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
        {
            FakeMemory m;
            m.PutTextBox(kObject, kText);
            m.PutString(kText, samples[i]);

            std::string out;
            test::Check(textbox::ReadString(Read32, Read16, &m, kObject, out),
                        std::string("reads \"") + samples[i] + "\"");
            test::EqualStr(out, samples[i], "exactly");
        }
    }

    void TestOffset(void)
    {
        test::Section("the string offset");

        // Established by walking live objects; the value is the whole finding.
        test::Equal(textbox::kStringOffset, 0xD4u, "string pointer sits at +0xD4");
    }

    void TestEmptyAndMissing(void)
    {
        test::Section("panes with nothing in them");

        FakeMemory m;
        m.PutTextBox(kObject, kText);
        m.PutString(kText, "");

        std::string out;
        test::Check(!textbox::ReadString(Read32, Read16, &m, kObject, out),
                    "an empty string reports failure rather than an empty utterance");

        FakeMemory noPtr;
        test::Check(!textbox::ReadString(Read32, Read16, &noPtr, kObject, out),
                    "an unreadable object fails cleanly");
    }

    void TestRejectsBadPointers(void)
    {
        test::Section("pointers that are not strings");

        std::string out;

        FakeMemory nullPtr;
        nullPtr.PutTextBox(kObject, 0);
        test::Check(!textbox::ReadString(Read32, Read16, &nullPtr, kObject, out),
                    "a null string pointer is refused");

        FakeMemory outside;
        outside.PutTextBox(kObject, 0x00100000);
        test::Check(!textbox::ReadString(Read32, Read16, &outside, kObject, out),
                    "a pointer into code is refused");

        FakeMemory m;
        m.PutTextBox(kObject, kText);
        test::Check(!textbox::ReadString(Read32, Read16, &m, 0x00100000, out),
                    "an object outside the heap is refused");
        test::Check(!textbox::ReadString(nullptr, Read16, &m, kObject, out),
                    "a null reader is refused");
    }

    void TestTerminators(void)
    {
        test::Section("terminators");

        FakeMemory m;
        m.PutTextBox(kObject, kText);
        m.PutString(kText, "SAVE");
        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::EqualStr(out, "SAVE", "NUL ends the string");

        // The games use 0xFFFF as a terminator in several structures, and it
        // is never a real character.
        FakeMemory ff;
        ff.PutTextBox(kObject, kText);
        for (u32 i = 0; i < 4; ++i)
            ff.halves[kText + i * 2] = static_cast<u16>("SAVE"[i]);
        ff.halves[kText + 8] = 0xFFFF;
        std::string out2;
        textbox::ReadString(Read32, Read16, &ff, kObject, out2);
        test::EqualStr(out2, "SAVE", "0xFFFF also ends the string");
    }

    void TestLineBreaks(void)
    {
        test::Section("line breaks");

        // A break is layout, not speech. Turning it into a space stops words
        // running together without inventing a pause the writer did not mean.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        const char *raw = "You got a\nmessage";
        for (u32 i = 0; raw[i]; ++i)
            m.halves[kText + i * 2] = static_cast<u16>(raw[i]);
        m.halves[kText + 17 * 2] = 0;

        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::EqualStr(out, "You got a message", "a newline becomes a space");
    }

    void TestNonAscii(void)
    {
        test::Section("non-ASCII");

        // Read out of the real game: country names carry accents.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        const u16 hungria[] = {'H', 'u', 'n', 'g', 'r', 0x00ED, 'a', 0};
        for (u32 i = 0; i < 8; ++i)
            m.halves[kText + i * 2] = hungria[i];

        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::EqualStr(out, "Hungria", "an accented letter folds to its base letter");
    }

    void TestRunawayString(void)
    {
        test::Section("a pointer that is not really a string");

        // Unterminated data must not be read forever.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        for (u32 i = 0; i < textbox::kMaxChars + 100; ++i)
            m.halves[kText + i * 2] = 'A';

        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::Equal(static_cast<u32>(out.size()), textbox::kMaxChars,
                    "reading stops at the cap");
    }

    void TestWorthSpeaking(void)
    {
        test::Section("deciding what is worth speaking");

        test::Check(textbox::WorthSpeaking("Your name?"), "a prompt");
        test::Check(textbox::WorthSpeaking("SAVE"), "a menu entry");
        test::Check(textbox::WorthSpeaking("You got a message"), "dialogue");

        // This exact string came out of the game, and speaking it would be
        // worse than silence.
        test::Check(!textbox::WorthSpeaking("-------------------"),
                    "a separator row of dashes is not speech");
        test::Check(!textbox::WorthSpeaking(""), "an empty string");
        test::Check(!textbox::WorthSpeaking("A"), "a single glyph used as an icon");
        test::Check(!textbox::WorthSpeaking("12345"), "digits alone are not language");
        test::Check(!textbox::WorthSpeaking("????????"), "unreadable text is not spoken");
        test::Check(textbox::WorthSpeaking("Hungr?a"),
                    "but one bad character in a real word is fine");
    }
    void TestGameTextCommands(void)
    {
        test::Section("the game's inline text commands");

        // Read verbatim out of a running Pokemon X: the language prompt has
        // command runs mid-sentence, and leading padding. Before these were
        // stripped the player heard stray question marks, which eSpeak speaks
        // as a question -- changing the intonation of a plain statement.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        const u16 prompt[] = {
            ' ', ' ', 'S', 'e', 't', '?',
            0x0010, 0x0002, 0x00E9, 0x0001,     // an inline command run
            'O', 'n', 'c', 'e', ' ', 's', 'e', 't', '.',
            0x0010, 0x0002, 0x00E9,
            0
        };
        for (u32 i = 0; i < sizeof(prompt) / sizeof(u16); ++i)
            m.halves[kText + i * 2] = prompt[i];

        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::EqualStr(out, "Set?Once set.", "commands stripped, padding trimmed");
    }

    void TestTypographicPunctuation(void)
    {
        test::Section("curly quotes and dashes");

        // The script uses these throughout. As '?' they were audible noise.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        const u16 quoted[] = {
            0x201C, 'H', 'i', 0x201D, ' ', 0x2014, ' ',
            'i', 't', 0x2019, 's', ' ', 'o', 'k', 0x2026, 0
        };
        for (u32 i = 0; i < sizeof(quoted) / sizeof(u16); ++i)
            m.halves[kText + i * 2] = quoted[i];

        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::EqualStr(out, "\"Hi\" - it's ok...", "folded to ASCII equivalents");
    }

    void TestPaddingOnly(void)
    {
        test::Section("a pane holding only padding");

        // Layout panes reserved but unused are full of spaces. Collapsing has
        // to leave them empty so they are never spoken.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        const u16 spaces[] = {' ', ' ', ' ', 0x0010, 0x0001, ' ', 0};
        for (u32 i = 0; i < sizeof(spaces) / sizeof(u16); ++i)
            m.halves[kText + i * 2] = spaces[i];

        std::string out;
        const bool ok = textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::Check(!ok, "reads as empty");
        test::EqualStr(out, "", "and yields nothing");
    }

    void TestUnknownGlyphsDropped(void)
    {
        test::Section("glyphs we cannot speak");

        // Button icons live in the private use area. Dropping them beats
        // reading them as question marks.
        FakeMemory m;
        m.PutTextBox(kObject, kText);
        const u16 withIcon[] = {'P', 'r', 'e', 's', 's', ' ', 0xE000, ' ', 'n', 'o', 'w', 0};
        for (u32 i = 0; i < sizeof(withIcon) / sizeof(u16); ++i)
            m.halves[kText + i * 2] = withIcon[i];

        std::string out;
        textbox::ReadString(Read32, Read16, &m, kObject, out);
        test::EqualStr(out, "Press now", "the icon is dropped and the gap closed");
    }
}

int main(void)
{
    std::printf("\non-screen text\n==============\n");

    TestOffset();
    TestReadsRealStrings();
    TestEmptyAndMissing();
    TestRejectsBadPointers();
    TestTerminators();
    TestLineBreaks();
    TestNonAscii();
    TestGameTextCommands();
    TestTypographicPunctuation();
    TestPaddingOnly();
    TestUnknownGlyphsDropped();
    TestRunawayString();
    TestWorthSpeaking();

    return test::Report("textbox");
}
