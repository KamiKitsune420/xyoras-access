/*
 * XYORAS Access — narrating the game's own text.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Ties together everything Phase 2 established:
 *
 *   vtscan      finds live nw::lyt::TextBox objects by their vptr
 *   panecache   decides how often that expensive scan is worth repeating
 *   textbox     reads the UTF-16 string each pane points at
 *   narration   decides which of them is worth saying
 *   speech      says it
 *
 * All of it runs on one dedicated thread. A single heap scan reads roughly
 * 3.6 million words, and even a routine poll reads every tracked pane; doing
 * either from the frame callback would be felt as stutter. Rule 3 in CLAUDE.md
 * is that speech never blocks the game, and the same has to hold for the
 * reading that feeds it.
 *
 * So the public surface is only flags. RequestReadScreen() and
 * RequestNewContext() set a bit and return; the polling thread notices on its
 * next pass. Nothing outside this file touches the cache or the narrator,
 * which means no lock is ever held across anything slow.
 *
 * UNTESTED ON HARDWARE. Each piece below has host tests, but this file -- the
 * threading and the CTRPF glue -- has never run on a console. The scan cost in
 * particular is unmeasured, and is the thing here most likely to need changing.
 */
#include "xyoras/common.hpp"
#include "xyoras/addresses.hpp"
#include "xyoras/diagnostics.hpp"
#include "xyoras/game.hpp"
#include "xyoras/narrate.hpp"
#include "xyoras/menuwindow.hpp"
#include "xyoras/narration.hpp"
#include "xyoras/panecache.hpp"
#include "xyoras/speech.hpp"
#include "xyoras/sync.hpp"
#include "xyoras/strbuf.hpp"
#include "xyoras/textbox.hpp"
#include "xyoras/vtscan.hpp"

#include <3ds.h>
#include <vector>

namespace xyoras { namespace features { namespace narrate {

namespace {

    constexpr size_t kThreadStackSize = 32 * 1024;

    /// Sleep between polls. Short enough to catch a message box as it types
    /// itself out, long enough to leave the game its memory bandwidth.
    constexpr u64 kPollIntervalNs = 16ull * 1000ull * 1000ull;   // ~16 ms

    /// Scans between sweeps for gfl::str::StrBuf objects. Discovery only --
    /// see the comment at the scan itself.
    constexpr u32 kStrBufScanEvery = 4;

    /// Ceiling on panes tracked at once. A text-heavy Gen 6 screen carries
    /// about 155. Polling thousands of addresses every 16 ms would be felt,
    /// so the cache is capped well above a real screen and far below a
    /// runaway.
    constexpr u32 kMaxPanes = 512;

    /// Sanity ceiling on a scan result. Above this the vtable address is
    /// wrong, not the screen busy, and narrating from it would be worse than
    /// staying quiet.
    constexpr u32 kImplausiblePaneCount = 4096;

    // -- owned by the polling thread alone -------------------------------------
    panecache::Cache    g_cache;
    narration::Narrator g_narrator;

    // -- shared with callers ---------------------------------------------------
    Mutex   g_lock;
    Thread  g_thread          = nullptr;
    bool    g_running         = false;
    bool    g_wantReadScreen  = false;
    bool    g_wantNewContext  = false;
    bool    g_wantLayoutDump  = false;
    u32     g_trackedPanes    = 0;

    /// Readers for the scanning and reading helpers. Those take an injected
    /// reader so the host tests can drive them against a fake address space;
    /// here it is the real thing, guarded so a bad address returns false
    /// rather than faulting.
    bool ReadWord(u32 address, u32 &out, void * /*ctx*/)
    {
        return game::Read32(address, out);
    }

    bool ReadHalf(u32 address, u16 &out, void * /*ctx*/)
    {
        return game::Read16(address, out);
    }

    /// Block reader for the scan. game::ReadBuf performs one permission check
    /// for the whole block, which is the entire point: the word-at-a-time scan
    /// would do 3.6 million of them, this does one per page.
    bool ReadBlock(u32 address, void *out, u32 size, void * /*ctx*/)
    {
        return game::ReadBuf(address, out, size);
    }

    /// Scratch for the scan. A page at a time, kept out of the thread stack --
    /// 16 KB of stack is not the place for it, and allocating per scan inside
    /// a game process is worth avoiding.
    u32 g_scanBuffer[vtscan::kPageSize / 4];

    // Declared here rather than beside their definitions: several of these
    // call each other, and the definition order does not match the call order.
    std::string Hex32(u32 value);
    void ProbeSiblingPanes(void);
    void SurveyAddressSpace(void);
    void FindTextPanesByShape(u32, u32);
    /// Set by RequestLayoutDump so a survey can be aimed at a chosen screen.
    volatile bool s_surveyRequested = false;

    void SurveyOnceIfDue(bool narrowed);
    void DumpPaneLayout(const std::vector<narration::Observation> &, bool);

    std::string Hex32(u32 value)
    {
        static const char kDigits[] = "0123456789ABCDEF";
        std::string out(8, '0');
        for (u32 i = 0; i < 8; ++i)
            out[7 - i] = kDigits[(value >> (i * 4)) & 0xF];
        return out;
    }

    /// The ARM11 system tick runs at 268.111856 MHz. Dividing by 268111 gives
    /// milliseconds closely enough for a cost that is being measured in tens
    /// of them.
    u32 TicksToMs(u64 ticks)
    {
        return static_cast<u32>(ticks / 268111ull);
    }

    /// Survey once, early, without waiting to be asked.
    ///
    /// One-shot and bounded. Normally a diagnostic that runs on its own is a
    /// bad idea -- automatic sampling starved the poll loop badly enough to
    /// hide a bug for a whole session -- but this one answers a question the
    /// mod cannot work without, and needing a keypress to get the answer is
    /// its own kind of failure.
    void SurveyOnceIfDue(bool narrowed)
    {
        if (!diag::IsNarrationTraceRequested())
            return;

        static u32 fullScans = 0;
        static bool done = false;

        // A survey can also be asked for again. The automatic one fires early,
        // on whatever screen the game happens to be showing -- which is the
        // boot sequence, drawn from code.bin. The classes that matter are in
        // the CRO modules loaded later (DllDialogCommon and friends), and they
        // only exist once the player is actually in one of those screens. So
        // the layout-dump chord re-arms this, letting the player point it at
        // the screen whose text is missing.
        const bool requested = s_surveyRequested;
        if (requested)
        {
            s_surveyRequested = false;
            done = false;
            fullScans = 3;
        }

        if (done)
            return;
        if (!requested && narrowed)
            return;                 // automatic surveys want a full-heap pass
        if (++fullScans < 3)        // let the game settle into a real screen
            return;

        done = true;
        diag::NarrationTrace("--- automatic one-shot survey ---");
        SurveyAddressSpace();
    }

    /// Full heap scan for text panes. Deliberately rare -- see panecache.hpp.
    void Rescan(void)
    {
        const u64 startTick = svcGetSystemTick();

        const u32 vtable = game::Addr(game::addr::kVtTextBox);
        const u32 vtStrBuf = game::Addr(game::addr::kVtStrBuf);
        if (vtable == 0)
        {
            // No verified address for this series. Stay quiet.
            g_cache.SetPanes(std::vector<u32>(), false);
            return;
        }

        // Scan only where panes were last found, when that is known. A full
        // scan was measured at 551 ms under emulation, which is far too much
        // to spend once a second; the window is typically a tiny fraction of
        // the heap. Every tenth scan goes wide anyway so panes allocated
        // outside the window are not missed forever.
        u32 start    = game::kHeapMin;
        u32 end      = game::kHeapMax;
        const bool narrowed = g_cache.NarrowScanRange(start, end);

        std::vector<u32> hits(kMaxPanes, 0);
        u32 found = vtscan::FindObjectsBlockwise(
            ReadBlock, nullptr, start, end, vtable,
            &hits[0], kMaxPanes, g_scanBuffer, vtscan::kPageSize / 4);

        // Dialogue is not a layout pane. Gen 6 draws its message box from a CRO
        // module and keeps the text in a gfl::str::StrBuf, so a TextBox-only
        // scan finds menus and prompts but never a conversation -- which is
        // exactly the symptom this chased for a long time. Scanning both kinds
        // into the same list is what makes dialogue narratable at all.
        // Scanning for string objects is expensive -- they are spread across
        // the heap, so unlike panes there is no tight region to narrow to, and
        // doing it every pass took a scan from 141 ms to 849 ms.
        //
        // It only ever *discovers* objects though. Once an address is cached it
        // is polled every 16 ms, and dialogue replaces the text inside the same
        // StrBuf rather than allocating a new one -- so a conversation is still
        // followed at full speed. Rediscovery can afford to be occasional.
        static u32 sbCountdown = 0;
        static std::vector<u32> sbLast;
        const bool scanStrings = (sbCountdown == 0);
        sbCountdown = scanStrings ? kStrBufScanEvery : (sbCountdown - 1);

        // On a skipped pass, carry forward what the last string scan found.
        // The cache is rebuilt from this list every time, so without this the
        // strings would fall out of it for three passes in four and dialogue
        // would stutter in and out of being narrated.
        if (!scanStrings)
        {
            for (u32 i = 0; i < sbLast.size() && found < kMaxPanes; ++i)
            {
                u32 vt = 0;
                if (game::Read32(sbLast[i], vt) && vt == vtStrBuf)
                    hits[found++] = sbLast[i];
            }
        }

        if (vtStrBuf != 0 && scanStrings && found < kMaxPanes)
        {
            // String objects sit in a different part of the heap from layout
            // panes, so reusing the pane region means scanning everything
            // between the two -- which took a narrow scan from 141 ms to 849 ms
            // and was felt directly as a delay before dialogue was spoken.
            // Remember where they actually were and look there instead.
            static u32 sbLow  = 0;
            static u32 sbHigh = 0;

            u32 sbStart = start;
            u32 sbEnd   = end;
            if (sbLow != 0 && sbHigh > sbLow)
            {
                sbStart = sbLow;
                sbEnd   = sbHigh;
            }

            u32 more = vtscan::FindObjectsBlockwise(
                ReadBlock, nullptr, sbStart, sbEnd, vtStrBuf,
                &hits[found], kMaxPanes - found, g_scanBuffer,
                vtscan::kPageSize / 4);

            // Nothing where they were last time means the screen changed under
            // us; fall back to the full region once to re-find them.
            if (more == 0 && sbLow != 0)
            {
                sbLow = sbHigh = 0;
                more = vtscan::FindObjectsBlockwise(
                    ReadBlock, nullptr, start, end, vtStrBuf,
                    &hits[found], kMaxPanes - found, g_scanBuffer,
                    vtscan::kPageSize / 4);
            }

            if (more > 0)
            {
                sbLast.assign(&hits[found], &hits[found] + more);

                u32 lo = hits[found];
                u32 hi = hits[found];
                for (u32 i = 1; i < more; ++i)
                {
                    const u32 a = hits[found + i];
                    if (a < lo) lo = a;
                    if (a > hi) hi = a;
                }
                // A page of slack either side, so an object allocated just
                // beside the others is still caught next time.
                sbLow  = (lo > 0x2000) ? (lo - 0x2000) : 0;
                sbHigh = hi + 0x2000;
            }

            found += more;
        }

        // FindObjects reports the true total even when it wrote fewer, which
        // is the point: "155" and "half a million" say very different things
        // about whether the vtable address is right.
        if (found == 0 || found >= kImplausiblePaneCount)
        {
            diag::NarrationTrace("scan: " + std::to_string(found) +
                                 " hits -- rejected, cache cleared, " +
                                 std::to_string(TicksToMs(svcGetSystemTick() - startTick)) +
                                 " ms");
            g_cache.SetPanes(std::vector<u32>(), narrowed);
            return;
        }

        // Explicit requests are served on any scan. The automatic one still
        // waits for a full-heap pass, but once the cache narrows its region
        // most scans are narrow -- so gating on that meant a survey the player
        // asked for could sit unserved indefinitely.
        SurveyOnceIfDue(narrowed);

        hits.resize(found < kMaxPanes ? found : kMaxPanes);
        diag::NarrationTrace("scan: " + std::to_string(found) + " panes found" +
                             (found > kMaxPanes
                                  ? ", tracking " + std::to_string(kMaxPanes)
                                  : "") +
                             (narrowed ? " (narrow)" : " (full heap)") +
                             " in " +
                             std::to_string(TicksToMs(svcGetSystemTick() - startTick)) +
                             " ms");
        g_cache.SetPanes(hits, narrowed);
    }

    /// Read every cached pane. Shared by the automatic poll and the
    /// read-screen request, so both see exactly the same screen.
    /// Announces the menu cursor, the way a screen reader should.
    ///
    /// A menu opening says how many options there are and reads the focused
    /// one; moving the cursor reads only the option landed on. That is the
    /// behaviour of nvgt's menu.nvgt and of NVDA speaking the focused object,
    /// and it is the opposite of what this mod did before, which was to read
    /// every option because every option had "changed".
    ///
    /// The label often cannot be resolved -- the entry payload's layout is not
    /// known yet -- so position is announced regardless. "4 of 7" alone is
    /// still navigable; silence is not.
    void PollMenu(std::vector<std::string> &toSpeak)
    {
        const u32 vt = game::Addr(game::addr::kVtMenuWindow);
        if (vt == 0)
            return;

        static u32 lastSelected = menuwindow::kNoSelection;
        static u32 lastCount    = 0;
        static u32 lastObject   = 0;

        std::vector<u32> hits(4, 0);
        const u32 found = vtscan::FindObjectsBlockwise(
            ReadBlock, nullptr, game::kHeapMin, game::kHeapMax, vt,
            &hits[0], 4, g_scanBuffer, vtscan::kPageSize / 4);

        if (found == 0)
        {
            lastSelected = menuwindow::kNoSelection;
            lastCount = 0;
            lastObject = 0;
            return;
        }

        menuwindow::State st;
        if (!menuwindow::Read(ReadWord, nullptr, hits[0], st))
            return;

        // A different menu object, or a different size, means a new menu rather
        // than movement within the current one.
        const bool isNewMenu = (hits[0] != lastObject) || (st.count != lastCount);

        if (!isNewMenu && st.selected == lastSelected)
            return;                     // nothing moved

        lastObject   = hits[0];
        lastCount    = st.count;
        lastSelected = st.selected;

        if (!st.HasFocus())
            return;                     // menu present, cursor not on anything yet

        std::string phrase;
        if (isNewMenu)
        {
            phrase = narration::Narrator::Count(st.count) + " items. ";
        }

        std::string label;
        const u32 payload = menuwindow::EntryPayload(st, st.selected);
        if (payload != 0)
        {
            u32 inner = 0;
            if (game::Read32(payload, inner) && mem::InHeap(inner))
                strbuf::ReadString(ReadWord, ReadHalf, nullptr, inner, label);
            if (label.empty())
                strbuf::ReadString(ReadWord, ReadHalf, nullptr, payload, label);
        }

        if (!label.empty())
            phrase += label + ". ";

        phrase += narration::Narrator::Count(st.selected + 1) + " of " +
                  narration::Narrator::Count(st.count) + ".";

        toSpeak.push_back(phrase);
    }

    u32 Observe(std::vector<narration::Observation> &observed)
    {
        const std::vector<u32> &panes = g_cache.Panes();

        observed.clear();
        observed.reserve(panes.size());

        u32 read = 0;
        for (u32 i = 0; i < panes.size(); ++i)
        {
            // A tracked address is either a layout pane or a GameFreak string
            // object, and the vtable at +0 says which. Dispatching on it means
            // the two kinds can share one cache and one narrator, instead of
            // duplicating the whole polling path.
            std::string text;
            u32 vt = 0;
            if (!ReadWord(panes[i], vt, nullptr))
                continue;       // freed since the scan

            bool got = false;
            if (vt == game::Addr(game::addr::kVtStrBuf))
                got = strbuf::ReadString(ReadWord, ReadHalf, nullptr, panes[i], text);
            else
                got = textbox::ReadString(ReadWord, ReadHalf, nullptr, panes[i], text);

            if (!got)
                continue;       // freed, or laid out but empty -- both normal
            ++read;
            observed.push_back(narration::Observation(panes[i], text));
        }
        return read;
    }


    /// One-shot hex dump of what a TextBox holds before its string pointer.
    ///
    /// Read-screen currently reports panes in heap-address order, which is
    /// allocation order and has nothing to do with where they are on screen.
    /// A menu read out in allocation order is confusing rather than useful.
    ///
    /// TextBox derives from nw::lyt::Pane, and a Pane carries a transform --
    /// so its screen position is somewhere in the 0xD4 bytes before the string
    /// pointer we already read. This dumps them alongside the text, so the
    /// offset can be identified by looking for the field that varies down a
    /// list the player can see the order of. Once found it goes in the address
    /// table and this goes away.
    ///
    /// Only ever runs once, and only with tracing on.
    void DumpPaneLayout(const std::vector<narration::Observation> &observed,
                        bool requested)
    {
        if (!diag::IsNarrationTraceRequested())
            return;

        // Only ever on request.
        //
        // This used to sample automatically every few seconds, and that
        // distorted the very thing it was measuring. Each dump writes a few
        // hundred lines through a file that is opened and closed per line, and
        // probes four classes over the whole heap. In a real play session it
        // starved the poll loop badly enough that only eight scans happened at
        // all -- which is how a stale cache went unnoticed for the whole
        // session. A diagnostic that changes the behaviour under test is worse
        // than no diagnostic.
        if (!requested)
            return;

        diag::NarrationTrace("--- snapshot requested ---");

        // More than a handful is unreadable and the file gets large.
        const u32 kMaxPanesToDump = 16;
        const u32 count = observed.size() < kMaxPanesToDump
                              ? static_cast<u32>(observed.size())
                              : kMaxPanesToDump;

        diag::NarrationTrace("layout dump: first " + std::to_string(count) +
                             " panes, words 0x00 to 0xD0");

        for (u32 i = 0; i < count; ++i)
        {
            diag::NarrationTrace("pane " + Hex32(observed[i].id) + "  \"" +
                                 observed[i].text + "\"");

            // Eight words a line, labelled with the offset, so a column can be
            // followed down the panes by eye.
            for (u32 base = 0; base < textbox::kStringOffset; base += 32)
            {
                std::string line = "  +" + Hex32(base).substr(6) + ":";
                for (u32 w = 0; w < 8 && base + w * 4 < textbox::kStringOffset; ++w)
                {
                    u32 value = 0;
                    line += " ";
                    line += game::Read32(observed[i].id + base + w * 4, value)
                                ? Hex32(value) : "--------";
                }
                diag::NarrationTrace(line);
            }
        }

        ProbeSiblingPanes();
        SurveyAddressSpace();
    }

    /// Unguarded block reader, for the survey below only.
    bool ReadBlockRaw(u32 address, void *out, u32 size, void * /*ctx*/)
    {
        return game::ReadBufUnguarded(address, out, size);
    }

    /// Counts layout objects across a much wider address range than the mod
    /// normally scans, reporting where they actually are.
    ///
    /// Why this exists: kHeapMin/kHeapMax are inherited numbers -- 0x08000000
    /// to 0x08DF0000, about 14 MB -- that this project has never verified. If
    /// the game allocates its in-game screens above that bound, the mod is
    /// blind to them and every scan would keep returning the same stale
    /// objects from inside the window, which is exactly the symptom observed:
    /// a full heap scan finding 21 TextBoxes belonging to a screen the player
    /// left several minutes ago.
    ///
    /// One megabyte per line, so the shape of the heap is visible rather than
    /// just a total.
    void SurveyAddressSpace(void)
    {
        struct Probe { const char *name; const game::AddrPair *vt; };
        // The dialogue family is included deliberately. A TextBox-only survey
        // says nothing about the message box, because the message box is not a
        // TextBox -- addresses.cpp has had app::tool::TalkWindow ("the dialogue
        // box itself") identified from RTTI for some time, but nothing has ever
        // scanned for it. Counting instances per screen is the cheapest way to
        // find out which of these actually exists while dialogue is showing.
        const Probe probes[] = {
            { "TextBox",     &game::addr::kVtTextBox     },
            { "Picture",     &game::addr::kVtPicture     },
            { "TalkWindow",  &game::addr::kVtTalkWindow  },
            { "MsgWin",      &game::addr::kVtMsgWin      },
            { "StrWin",      &game::addr::kVtStrWin      },
            { "MenuWindow",  &game::addr::kVtMenuWindow  },
        };

        // Well past anything the mod currently looks at. The permission check
        // makes unmapped regions cheap and safe.
        const u32 kSurveyStart = 0x08000000;
        const u32 kSurveyEnd   = 0x10000000;   // top of the APPLICATION region
        const u32 kBucket      = 0x100000;      // 1 MB

        for (u32 p = 0; p < sizeof(probes) / sizeof(probes[0]); ++p)
        {
            const u32 vtable = game::Addr(*probes[p].vt);
            if (vtable == 0)
                continue;

            u32 total = 0;
            u32 outside = 0;
            std::string line;

            for (u32 base = kSurveyStart; base < kSurveyEnd; base += kBucket)
            {
                const u32 found = vtscan::FindObjectsBlockwise(
                    ReadBlockRaw, nullptr, base, base + kBucket, vtable,
                    nullptr, 0, g_scanBuffer, vtscan::kPageSize / 4);

                if (found == 0)
                    continue;

                total += found;
                if (base < game::kHeapMin || base >= game::kHeapMax)
                    outside += found;

                line = std::string("  ") + Hex32(base) + "  " +
                       std::to_string(found);
                if (base < game::kHeapMin || base >= game::kHeapMax)
                    line += "   <-- OUTSIDE the window the mod scans";
                diag::NarrationTrace(line);

                // Pictures mark where a live screen is. If text panes are
                // hiding anywhere, they are hiding beside them.
                if (p == 1 && found >= 8)
                    FindTextPanesByShape(base, base + kBucket);
            }

            diag::NarrationTrace(std::string("survey ") + probes[p].name + ": " +
                                 std::to_string(total) + " total, " +
                                 std::to_string(outside) + " outside the window");
        }
    }

    /// Finds text panes by SHAPE rather than by class.
    ///
    /// The survey showed an active layout arena holding 87 Pictures and zero
    /// TextBoxes. A screen made of images with no text does not exist, so the
    /// text panes are there and simply are not `nw::lyt::TextBox` -- almost
    /// certainly a subclass defined in one of the CRO modules the game loads
    /// at runtime, whose vtable is not in code.bin and therefore can never be
    /// found by scanning for a code.bin address.
    ///
    /// Scanning for the class is a dead end. Scanning for the SHAPE is not:
    /// whatever the class, a text pane holds a pointer to its UTF-16 string at
    /// +0xD4. So this looks for words that point at UTF-16 text, treats the
    /// word's address minus 0xD4 as the object, and reports what vtable that
    /// object has. Those vtables are the text classes actually in use.
    ///
    /// Restricted to one region, because the check is a read per candidate and
    /// that is far too expensive to do over the whole heap.
    void FindTextPanesByShape(u32 regionStart, u32 regionEnd)
    {
        // Distinct vtables seen, with how many objects had each.
        const u32 kMaxClasses = 16;
        u32 vtables[kMaxClasses] = {0};
        u32 counts[kMaxClasses]  = {0};
        std::string samples[kMaxClasses];
        u32 classCount = 0;
        u32 candidates = 0;

        for (u32 base = regionStart; base < regionEnd; base += vtscan::kPageSize)
        {
            if (!game::ReadBufUnguarded(base, g_scanBuffer, vtscan::kPageSize))
                continue;

            for (u32 i = 0; i < vtscan::kPageSize / 4; ++i)
            {
                const u32 candidate = g_scanBuffer[i];

                // A string pointer: into the heap, and 2-byte aligned.
                if (!game::InHeap(candidate) || (candidate & 1) != 0)
                    continue;

                // The object would start 0xD4 before this word.
                const u32 wordAddr = base + i * 4;
                if (wordAddr < textbox::kStringOffset)
                    continue;
                const u32 object = wordAddr - textbox::kStringOffset;
                if (!game::InHeap(object))
                    continue;

                // Does it actually point at readable UTF-16 ASCII?
                std::string text;
                if (!textbox::ReadString(ReadWord, ReadHalf, nullptr, object, text))
                    continue;
                // WorthSpeaking is deliberately generous -- a two-letter menu
                // label is worth saying. That is far too loose here: over a
                // megabyte of arbitrary data, plenty of it reads as a short
                // ASCII pair by chance, and the first run of this drowned its
                // one real hit in "YY", "hm" and "ks". Real interface text is
                // longer and contains a space or a good run of letters.
                if (text.size() < 6)
                    continue;
                u32 letters = 0;
                bool space = false;
                for (u32 c = 0; c < text.size(); ++c)
                {
                    const char ch = text[c];
                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
                        ++letters;
                    else if (ch == ' ')
                        space = true;
                }
                if (letters < 5 || (!space && letters < 8))
                    continue;

                ++candidates;

                u32 vptr = 0;
                if (!game::Read32(object, vptr))
                    continue;

                u32 slot = 0;
                for (; slot < classCount; ++slot)
                    if (vtables[slot] == vptr)
                        break;

                if (slot == classCount)
                {
                    if (classCount >= kMaxClasses)
                        continue;
                    vtables[classCount] = vptr;
                    samples[classCount] = text;
                    ++classCount;
                }
                ++counts[slot];
            }
        }

        diag::NarrationTrace("shape scan " + Hex32(regionStart) + "-" +
                             Hex32(regionEnd) + ": " +
                             std::to_string(candidates) + " text-like objects, " +
                             std::to_string(classCount) + " distinct classes");

        for (u32 i = 0; i < classCount; ++i)
        {
            const bool inCode = vtscan::PlausibleVtable(vtables[i]);
            diag::NarrationTrace("  vptr " + Hex32(vtables[i]) + "  x" +
                                 std::to_string(counts[i]) +
                                 (inCode ? "  (in code.bin)" : "  (NOT in code.bin -- a CRO class)") +
                                 "  e.g. \"" + samples[i] + "\"");
        }
    }

    /// Scans for the other NintendoWare layout classes and reports where they
    /// are on screen.
    ///
    /// Text is only half of a screen. A menu cursor, a highlight bar and a
    /// selected-item frame are Pictures and Windows, not TextBoxes, so "which
    /// item is selected" -- the single most useful thing a menu can tell a
    /// blind player -- is invisible to a TextBox-only scan.
    ///
    /// The hypothesis being tested: a cursor Picture sits at the same vertical
    /// position as the row it is pointing at. If one Picture's ty matches a
    /// language row's ty, selection can be read straight off the layout with
    /// no code hooks and no save data.
    ///
    /// Exploratory. Delete once it has answered its question.
    void ProbeSiblingPanes(void)
    {
        struct Probe { const char *name; const game::AddrPair *vt; };
        const Probe probes[] = {
            { "Picture",  &game::addr::kVtPicture  },
            { "Window",   &game::addr::kVtWindow   },
            { "Pane",     &game::addr::kVtPane     },
            { "Layout",   &game::addr::kVtLayout   },
            // The dialogue and menu owners. Geometry tells us nothing useful
            // about these -- their layout is unknown -- so they get a raw word
            // dump instead, which is how the text pointer will be found.
            { "MsgWin",     &game::addr::kVtMsgWin     },
            { "MenuWindow", &game::addr::kVtMenuWindow },
            // gfl::str::StrBuf. Its layout is now known from the constructor at
            // 0x0038E4A8: buffer pointer at +4, capacity and length packed into
            // the word at +8, UTF-16. Previously dismissed as a pool of empty
            // formatting buffers -- which most of them are -- but the dialogue
            // text has to live in one of them.
            { "StrBuf",     &game::addr::kVtStrBuf     },
        };

        for (u32 p = 0; p < sizeof(probes) / sizeof(probes[0]); ++p)
        {
            const u32 vtable = game::Addr(*probes[p].vt);
            if (vtable == 0)
                continue;

            std::vector<u32> hits(kMaxPanes, 0);
            const u32 found = vtscan::FindObjectsBlockwise(
                ReadBlock, nullptr, game::kHeapMin, game::kHeapMax, vtable,
                &hits[0], kMaxPanes, g_scanBuffer, vtscan::kPageSize / 4);

            diag::NarrationTrace(std::string("probe ") + probes[p].name + ": " +
                                 std::to_string(found) + " live");

            // Positions only. Enough to correlate against the text rows,
            // without pages of hex for objects we cannot name.
            const u32 show = found < 96 ? found : 96;
            for (u32 i = 0; i < show; ++i)
            {
                u32 tx = 0, ty = 0, w = 0, h = 0;
                game::Read32(hits[i] + game::addr::kPaneTranslateX, tx);
                game::Read32(hits[i] + game::addr::kPaneTranslateY, ty);
                game::Read32(hits[i] + game::addr::kPaneSizeW, w);
                game::Read32(hits[i] + game::addr::kPaneSizeH, h);

                diag::NarrationTrace("  " + Hex32(hits[i]) +
                                     "  tx=" + Hex32(tx) +
                                     " ty=" + Hex32(ty) +
                                     " w=" + Hex32(w) +
                                     " h=" + Hex32(h));

                // For the classes whose layout we do not know yet, print the
                // object itself. A pointer into the heap followed by readable
                // UTF-16 is what the message text will look like.
                // StrBuf has a known layout, so read it rather than dumping it.
                if (probes[p].vt == &game::addr::kVtStrBuf)
                {
                    std::string text;
                    if (strbuf::ReadString(ReadWord, ReadHalf, nullptr, hits[i], text))
                    {
                        diag::NarrationTrace("    " + Hex32(hits[i]) +
                                             "  STRBUF \"" + text + "\"");
                    }
                    continue;
                }

                const bool unknownLayout =
                    (probes[p].vt == &game::addr::kVtMsgWin ||
                     probes[p].vt == &game::addr::kVtMenuWindow);

                if (unknownLayout)
                {
                    for (u32 base = 0; base < 0x80; base += 32)
                    {
                        std::string row = "    +" + Hex32(base).substr(6) + ":";
                        for (u32 w2 = 0; w2 < 8; ++w2)
                        {
                            u32 value = 0;
                            row += " " + (game::Read32(hits[i] + base + w2 * 4, value)
                                              ? Hex32(value) : std::string("--------"));
                        }
                        diag::NarrationTrace(row);
                    }

                    // Follow every field that looks like a heap pointer and try
                    // to read a string there. The message text is reached by a
                    // chain of these, and one hop is usually enough to see it:
                    // readable UTF-16 is unmistakable next to raw fields.
                    for (u32 f = 4; f < 0x30; f += 4)
                    {
                        u32 ptr = 0;
                        if (!game::Read32(hits[i] + f, ptr))
                            continue;
                        if (ptr < game::kHeapMin || ptr >= game::kHeapMax)
                            continue;

                        // ReadString expects a TextBox and adds kStringOffset
                        // itself, so bias the address to make the pointer we
                        // are testing land where it expects the string.
                        std::string text;
                        const bool got = textbox::ReadString(
                            ReadWord, ReadHalf, nullptr,
                            ptr - textbox::kStringOffset, text);
                        if (got && !text.empty() && textbox::WorthSpeaking(text))
                        {
                            diag::NarrationTrace("    +" + Hex32(f).substr(6) +
                                                 " -> " + Hex32(ptr) +
                                                 "  TEXT \"" + text + "\"");
                            continue;
                        }

                        std::string row = "    +" + Hex32(f).substr(6) + " -> " +
                                          Hex32(ptr) + ":";
                        for (u32 w2 = 0; w2 < 6; ++w2)
                        {
                            u32 value = 0;
                            row += " " + (game::Read32(ptr + w2 * 4, value)
                                              ? Hex32(value) : std::string("--------"));
                        }
                        diag::NarrationTrace(row);
                    }
                }
            }
        }
    }

    /// Speak everything on screen, because the player asked for it.
    void DoReadScreen(const std::vector<narration::Observation> &observed)
    {
        std::vector<std::string> all;
        g_narrator.ReadAll(observed, all);

        if (all.empty())
        {
            diag::NarrationTrace("read screen: nothing worth speaking out of " +
                                 std::to_string(observed.size()) + " panes read");
            speech::Say(speech::Priority::Interrupt, "Nothing to read.");
            return;
        }

        diag::NarrationTrace("read screen: " + std::to_string(all.size()) +
                             " of " + std::to_string(observed.size()) +
                             " panes worth speaking");
        for (u32 i = 0; i < all.size(); ++i)
            diag::NarrationTrace("  | " + all[i]);

        // The first line cancels whatever was queued, since the player has
        // just asked for something else; the rest follow it in order.
        speech::Say(speech::Priority::Interrupt, all[0]);
        for (u32 i = 1; i < all.size(); ++i)
            speech::Say(speech::Priority::Dialogue, all[i]);
    }

    /// One pass: take pending requests, scan if due, read, decide, speak.
    void PollOnce(void)
    {
        bool readScreen = false;
        bool layoutDump = false;
        {
            Lock lock(g_lock);
            readScreen       = g_wantReadScreen;
            g_wantReadScreen = false;
            layoutDump       = g_wantLayoutDump;
            g_wantLayoutDump = false;

            if (g_wantNewContext)
            {
                g_wantNewContext = false;
                g_narrator.NewContext();
                g_cache.Invalidate();
            }
        }

        if (g_cache.NeedsScan())
            Rescan();

        std::vector<narration::Observation> observed;
        const u32 read = Observe(observed);
        g_cache.NotePollResult(read);

        if (readScreen)
        {
            // Deliberately not also narrating changes on this pass: the player
            // is about to hear the whole screen, and repeating one line of it
            // immediately afterwards is just noise.
            DoReadScreen(observed);
            return;
        }

        const bool wasBaseline = g_narrator.BaselinePending();

        std::vector<std::string> toSpeak;
        g_narrator.Poll(observed, toSpeak);

        // The baseline poll is silent by design, which makes it the one moment
        // where nothing in the trace says what the mod is actually seeing. Log
        // it: "what does it read on this screen" is the first question anyone
        // asks when a screen does not get spoken.
        if (wasBaseline && !g_narrator.BaselinePending())
        {
            diag::NarrationTrace("baseline: " + std::to_string(observed.size()) +
                                 " panes read");
            for (u32 i = 0; i < observed.size(); ++i)
                diag::NarrationTrace("  . " + observed[i].text);
        }

        DumpPaneLayout(observed, layoutDump);

        for (u32 i = 0; i < toSpeak.size(); ++i)
        {
            diag::NarrationTrace("say: " + toSpeak[i]);
            speech::Say(speech::Priority::Dialogue, toSpeak[i]);
        }

        // The menu cursor is read separately and at Ui priority, because it is
        // the player moving rather than the game speaking. Ui replaces a
        // pending Ui item, so running the cursor down a list does not build a
        // backlog -- whereas Dialogue queues and would.
        std::vector<std::string> menuSpeak;
        PollMenu(menuSpeak);
        for (u32 i = 0; i < menuSpeak.size(); ++i)
        {
            diag::NarrationTrace("menu: " + menuSpeak[i]);
            speech::Say(speech::Priority::Ui, menuSpeak[i]);
        }
    }

    bool ShouldKeepRunning(void)
    {
        Lock lock(g_lock);
        return g_running;
    }

    void ThreadMain(void *)
    {
        while (ShouldKeepRunning())
        {
            PollOnce();

            {
                Lock lock(g_lock);
                g_trackedPanes = g_cache.Count();
            }

            svcSleepThread(kPollIntervalNs);
        }
    }

    /// Runs below the game's own threads so polling never preempts rendering.
    /// On the 3DS a HIGHER number is a LOWER priority.
    s32 PollThreadPriority(void)
    {
        s32 self = 0x30;
        if (R_FAILED(svcGetThreadPriority(&self, CUR_THREAD_HANDLE)))
            self = 0x30;

        s32 wanted = self + 1;
        if (wanted > 0x3F)      // lowest an application-range thread may be
            wanted = 0x3F;
        return wanted;
    }
}

bool Start(void)
{
    {
        Lock lock(g_lock);
        if (g_running)
            return true;
    }

    // Reading game memory is meaningless if the offsets belong to a build we
    // have not verified against. Silence beats confident nonsense.
    if (!game::IsVerified(game::Capability::LayoutText))
        return false;

    // And there is no point starting if we have no address for this series.
    if (game::Addr(game::addr::kVtTextBox) == 0)
        return false;

    {
        Lock lock(g_lock);
        g_running = true;
    }

    diag::NarrationTrace("--- narration starting, TextBox vtable " +
                         std::to_string(game::Addr(game::addr::kVtTextBox)) +
                         " ---");

    g_thread = threadCreate(ThreadMain, nullptr, kThreadStackSize,
                            PollThreadPriority(), -1, false);

    if (g_thread == nullptr)
    {
        Lock lock(g_lock);
        g_running = false;
        return false;
    }

    return true;
}

void Stop(void)
{
    Thread thread = nullptr;
    {
        Lock lock(g_lock);
        if (!g_running)
            return;
        g_running = false;
        thread    = g_thread;
        g_thread  = nullptr;
    }

    if (thread != nullptr)
    {
        // The poll loop sleeps 16 ms at a time, so this returns promptly.
        threadJoin(thread, U64_MAX);
        threadFree(thread);
    }
}

void RequestReadScreen(void)
{
    Lock lock(g_lock);
    g_wantReadScreen = true;
}

void RequestNewContext(void)
{
    Lock lock(g_lock);
    g_wantNewContext = true;
}

void RequestLayoutDump(void)
{
    Lock lock(g_lock);
    g_wantLayoutDump = true;

    // Also re-arm the address-space survey. The dump answers "what is the
    // narrator tracking on this screen"; the survey answers the harder
    // question behind it -- "what text-like classes exist here that it is NOT
    // tracking", which is how a CRO module's TextBox gets found at all.
    s_surveyRequested = true;
}

u32 TrackedPanes(void)
{
    Lock lock(g_lock);
    return g_trackedPanes;
}

bool IsRunning(void)
{
    Lock lock(g_lock);
    return g_running;
}

}}} // namespace xyoras::features::narrate
