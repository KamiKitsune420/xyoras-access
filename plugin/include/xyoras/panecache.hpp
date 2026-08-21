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
 * addresses often. This file holds the policy for when to scan again, which is
 * the part worth testing.
 *
 * Objects are freed as the player moves between screens, so a cached address
 * eventually stops being a pane. The cache notices when reads start failing
 * and asks for a rescan rather than quietly narrating nothing.
 */
#ifndef XYORAS_PANECACHE_HPP
#define XYORAS_PANECACHE_HPP

#include "xyoras/common.hpp"

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

    /// Tracks a set of pane addresses and decides when to look for them again.
    class Cache
    {
    public:
        Cache() : sinceScan_(kRescanInterval), forced_(true) {}

        /// True when the caller should perform a full scan this poll.
        bool NeedsScan() const
        {
            return forced_ || sinceScan_ >= kRescanInterval;
        }

        /// Replace the cached set after a scan.
        void SetPanes(const std::vector<u32> &panes)
        {
            panes_ = panes;
            sinceScan_ = 0;
            forced_ = false;
        }

        /// Record the result of a poll: how many cached panes were read
        /// successfully. A cache that has mostly gone bad triggers a rescan
        /// rather than leaving the player with stale or silent output.
        void NotePollResult(u32 succeeded)
        {
            ++sinceScan_;

            if (panes_.empty())
            {
                forced_ = true;
                return;
            }

            const u32 failed = static_cast<u32>(panes_.size()) - succeeded;
            if (failed * kStaleDenominator >= static_cast<u32>(panes_.size()) * kStaleNumerator)
                forced_ = true;
        }

        /// Force a scan on the next poll -- used when the game changes context.
        void Invalidate() { forced_ = true; }

        const std::vector<u32> &Panes() const { return panes_; }
        u32 Count() const { return static_cast<u32>(panes_.size()); }

    private:
        std::vector<u32> panes_;
        u32  sinceScan_;
        bool forced_;
    };

}} // namespace xyoras::panecache

#endif
