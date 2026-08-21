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

    /// Full heap scan for text panes. Deliberately rare -- see panecache.hpp.
    void Rescan(void)
    {
        const u32 vtable = game::Addr(game::addr::kVtTextBox);
        if (vtable == 0)
        {
            // No verified address for this series. Stay quiet.
            g_cache.SetPanes(std::vector<u32>());
            return;
        }

        std::vector<u32> hits(kMaxPanes, 0);
        const u32 found = vtscan::FindObjectsBlockwise(
            ReadBlock, nullptr, game::kHeapMin, game::kHeapMax, vtable,
            &hits[0], kMaxPanes, g_scanBuffer, vtscan::kPageSize / 4);

        // FindObjects reports the true total even when it wrote fewer, which
        // is the point: "155" and "half a million" say very different things
        // about whether the vtable address is right.
        if (found == 0 || found >= kImplausiblePaneCount)
        {
            g_cache.SetPanes(std::vector<u32>());
            return;
        }

        hits.resize(found < kMaxPanes ? found : kMaxPanes);
        g_cache.SetPanes(hits);
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

    /// Speak everything on screen, because the player asked for it.
    void DoReadScreen(const std::vector<narration::Observation> &observed)
    {
        std::vector<std::string> all;
        g_narrator.ReadAll(observed, all);

        if (all.empty())
        {
            speech::Say(speech::Priority::Interrupt, "Nothing to read.");
            return;
        }

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
        {
            Lock lock(g_lock);
            readScreen       = g_wantReadScreen;
            g_wantReadScreen = false;

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

        std::vector<std::string> toSpeak;
        g_narrator.Poll(observed, toSpeak);

        for (u32 i = 0; i < toSpeak.size(); ++i)
            speech::Say(speech::Priority::Dialogue, toSpeak[i]);
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
    if (!game::IsVersionSupported())
        return false;

    // And there is no point starting if we have no address for this series.
    if (game::Addr(game::addr::kVtTextBox) == 0)
        return false;

    {
        Lock lock(g_lock);
        g_running = true;
    }

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
