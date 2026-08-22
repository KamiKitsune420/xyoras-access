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
#include "xyoras/narration.hpp"
#include "xyoras/panecache.hpp"
#include "xyoras/speech.hpp"
#include "xyoras/sync.hpp"
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
    void SurveyOnceIfDue(void);
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
    void SurveyOnceIfDue(void)
    {
        if (!diag::IsNarrationTraceRequested())
            return;

        static u32 fullScans = 0;
        static bool done = false;

        if (done)
            return;
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
        const u32 found = vtscan::FindObjectsBlockwise(
            ReadBlock, nullptr, start, end, vtable,
            &hits[0], kMaxPanes, g_scanBuffer, vtscan::kPageSize / 4);

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

        if (!narrowed)
            SurveyOnceIfDue();

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
    u32 Observe(std::vector<narration::Observation> &observed)
    {
        const std::vector<u32> &panes = g_cache.Panes();

        observed.clear();
        observed.reserve(panes.size());

        u32 read = 0;
        for (u32 i = 0; i < panes.size(); ++i)
        {
            std::string text;
            if (!textbox::ReadString(ReadWord, ReadHalf, nullptr, panes[i], text))
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
        const Probe probes[] = {
            { "TextBox", &game::addr::kVtTextBox },
            { "Picture", &game::addr::kVtPicture },
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
