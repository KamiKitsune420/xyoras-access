/*
 * XYORAS Access — keeping track of which text panes to poll.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Finding text panes means scanning the heap for a vptr, and that is far too
 * slow to do every frame -- it walks millions of words. But the panes have to
 * be read constantly, because a message box types its text out and we need to
 * see it settle.
 *
 * So the two are separated: scan rarely to find the objects, then poll those
 * addresses often. This file holds the policy for when to scan again, and how
 * much of the heap to scan, which is the part worth testing.
 *
 * Objects are freed as the player moves between screens, so a cached address
 * eventually stops being a pane. The cache notices when reads start failing
 * and asks for a rescan rather than quietly narrating nothing.
 *
 * A full scan was measured at **551 ms** in a running Pokemon X under
 * emulation (see "AI docks/12-research-log.md"). That is not a hardware
 * figure -- emulated memory access is far more expensive -- but it is enough
 * to rule out scanning the whole heap once a second. Panes cluster in a small
 * part of the heap, so after a scan finds them the cache remembers where they
 * were and asks for a much narrower scan next time.
 */
#ifndef XYORAS_PANECACHE_HPP
#define XYORAS_PANECACHE_HPP

#include "xyoras/common.hpp"
#include "xyoras/memchain.hpp"

#include <vector>

namespace xyoras { namespace panecache {

    /// Polls between routine rescans. At roughly 60 Hz this is about a second
    /// -- often enough to notice a new screen quickly, rare enough that the
    /// cost of scanning is spread thin.
    constexpr u32 kRescanInterval = 60;

    /// Proportion of cached panes that must fail to read before the cache is
    /// treated as stale. One or two failures is normal churn; most of them
    /// failing means the screen has changed underneath us.
    constexpr u32 kStaleNumerator = 1;
    constexpr u32 kStaleDenominator = 2;   ///< i.e. half

    /// Narrow scans in a row before one full scan is done anyway.
    ///
    /// A narrow scan cannot find panes allocated outside the window it learned,
    /// so the window has to be rebuilt from scratch periodically or the mod
    /// would go permanently blind to a part of the heap it once ignored.
    constexpr u32 kNarrowScansBeforeFull = 10;

    /// Slack added to each side of the learned window, so panes allocated just
    /// beside the known ones are still found without a full scan.
    constexpr u32 kWindowPadding = 0x20000;   // 128 KB

    /// Tracks a set of pane addresses and decides when, and where, to look for
    /// them again.
    class Cache
    {
    public:
        Cache()
            : sinceScan_(kRescanInterval), forced_(true),
              windowStart_(0), windowEnd_(0),
              haveWindow_(false), narrowScans_(0) {}

        /// True when the caller should perform a scan this poll.
        bool NeedsScan() const
        {
            return forced_ || sinceScan_ >= kRescanInterval;
        }

        /// Where the next scan should look.
        ///
        /// Returns false when the whole heap must be scanned: nothing learned
        /// yet, too many narrow scans in a row, or a narrow scan that came back
        /// empty. Returns true and fills the range otherwise.
        bool NarrowScanRange(u32 &start, u32 &end) const
        {
            if (!haveWindow_ || narrowScans_ >= kNarrowScansBeforeFull)
                return false;

            start = windowStart_;
            end   = windowEnd_;
            return true;
        }

        /// Replace the cached set after a scan.
        ///
        /// `narrowed` says whether the scan used NarrowScanRange. It matters
        /// because a narrow scan that finds nothing proves only that the panes
        /// are not where they were, and the answer to that is a full scan --
        /// whereas a full scan that finds nothing means there is genuinely no
        /// text on screen, which is normal during a transition.
        void SetPanes(const std::vector<u32> &panes, bool narrowed)
        {
            panes_     = panes;
            sinceScan_ = 0;
            forced_    = false;

            if (narrowed)
            {
                ++narrowScans_;
                if (panes_.empty())
                {
                    // The window is stale. Go wide next time.
                    haveWindow_  = false;
                    narrowScans_ = 0;
                    forced_      = true;
                }
            }
            else
            {
                narrowScans_ = 0;
            }

            if (!panes_.empty())
                LearnWindow();
        }

        /// Record the result of a poll: how many cached panes were read
        /// successfully. A cache that has mostly gone bad triggers a rescan
        /// rather than leaving the player with stale or silent output.
        void NotePollResult(u32 succeeded)
        {
            ++sinceScan_;

            if (panes_.empty())
            {
                // A scan just told us there is nothing on screen, which is
                // normal during a transition. Asking for another scan straight
                // away would run a full scan every poll, back to back, for as
                // long as the transition lasts. The interval is what keeps it
                // trying.
                return;
            }

            const u32 failed = static_cast<u32>(panes_.size()) - succeeded;
            if (failed * kStaleDenominator >= static_cast<u32>(panes_.size()) * kStaleNumerator)
                forced_ = true;
        }

        /// Force a scan on the next poll -- used when the game changes context.
        ///
        /// The learned window is kept: a new screen allocates its panes from
        /// the same heap as the old one, and throwing the window away would
        /// mean a full scan at exactly the moment the player is waiting to hear
        /// what changed.
        void Invalidate() { forced_ = true; }

        /// Forget where panes were last seen, so the next scan covers
        /// everything. For when the window is no longer to be trusted at all.
        void ForgetWindow()
        {
            haveWindow_  = false;
            narrowScans_ = 0;
            forced_      = true;
        }

        const std::vector<u32> &Panes() const { return panes_; }
        u32 Count() const { return static_cast<u32>(panes_.size()); }

        bool HaveWindow() const { return haveWindow_; }

    private:
        /// Remember the span the panes were found in, padded and page-aligned.
        void LearnWindow()
        {
            u32 lo = panes_[0];
            u32 hi = panes_[0];
            for (u32 i = 1; i < panes_.size(); ++i)
            {
                if (panes_[i] < lo) lo = panes_[i];
                if (panes_[i] > hi) hi = panes_[i];
            }

            // Pad, without wrapping past the ends of the heap.
            lo = (lo - mem::kHeapMin > kWindowPadding) ? lo - kWindowPadding
                                                       : mem::kHeapMin;
            hi = (mem::kHeapMax - hi > kWindowPadding) ? hi + kWindowPadding
                                                       : mem::kHeapMax;

            // Scans read whole pages, so the range must start on one.
            windowStart_ = lo & ~0xFFFu;
            windowEnd_   = hi;
            haveWindow_  = true;
        }

        std::vector<u32> panes_;
        u32  sinceScan_;
        bool forced_;

        u32  windowStart_;
        u32  windowEnd_;
        bool haveWindow_;
        u32  narrowScans_;
    };

}} // namespace xyoras::panecache

#endif
