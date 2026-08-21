/*
 * XYORAS Access — host tests for the pane cache policy.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The cost being managed here is real: finding text panes means scanning
 * millions of words of heap, and it cannot happen every frame. But panes are
 * freed as the player moves around, so a cache that never notices going stale
 * would leave the mod reading addresses that are no longer text -- silence, or
 * worse, nonsense.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/panecache.hpp"

#include <vector>

using namespace xyoras;

namespace {

    std::vector<u32> MakePanes(u32 count)
    {
        std::vector<u32> v;
        for (u32 i = 0; i < count; ++i)
            v.push_back(0x08100000 + i * 0x100);
        return v;
    }

    void TestScansFirst(void)
    {
        test::Section("the first poll scans");

        panecache::Cache c;
        test::Check(c.NeedsScan(), "a fresh cache asks for a scan");
        test::Equal(c.Count(), 0u, "and holds nothing yet");

        c.SetPanes(MakePanes(10));
        test::Check(!c.NeedsScan(), "after scanning it does not ask again");
        test::Equal(c.Count(), 10u, "and holds the panes");
    }

    void TestRescanInterval(void)
    {
        test::Section("routine rescans");

        panecache::Cache c;
        c.SetPanes(MakePanes(10));

        // All reads succeeding: only the interval should trigger a rescan.
        for (u32 i = 0; i < panecache::kRescanInterval - 1; ++i)
        {
            c.NotePollResult(10);
            if (c.NeedsScan())
            {
                test::Check(false, "no early rescan while everything reads");
                return;
            }
        }
        test::Check(!c.NeedsScan(), "quiet until the interval elapses");

        c.NotePollResult(10);
        test::Check(c.NeedsScan(), "then asks for a rescan");
    }

    void TestStaleCacheForcesRescan(void)
    {
        test::Section("a cache that has gone stale");

        // The player walked to a new screen and the old panes were freed.
        panecache::Cache c;
        c.SetPanes(MakePanes(10));

        c.NotePollResult(2);   // 8 of 10 failed
        test::Check(c.NeedsScan(), "mostly-failing reads force a rescan immediately");
    }

    void TestChurnDoesNotForceRescan(void)
    {
        test::Section("ordinary churn");

        // A pane or two disappearing is normal and must not cost a full scan
        // every poll.
        panecache::Cache c;
        c.SetPanes(MakePanes(10));

        c.NotePollResult(9);
        test::Check(!c.NeedsScan(), "one failure is tolerated");
        c.NotePollResult(8);
        test::Check(!c.NeedsScan(), "two failures are tolerated");
    }

    void TestHalfFailingIsStale(void)
    {
        test::Section("the staleness threshold");

        panecache::Cache c;
        c.SetPanes(MakePanes(10));
        c.NotePollResult(5);        // exactly half failed
        test::Check(c.NeedsScan(), "half failing counts as stale");

        panecache::Cache c2;
        c2.SetPanes(MakePanes(10));
        c2.NotePollResult(6);       // 4 of 10 failed
        test::Check(!c2.NeedsScan(), "just under half does not");
    }

    void TestEmptyResultForcesRescan(void)
    {
        test::Section("finding nothing");

        // A scan that returns nothing must not settle into never scanning
        // again -- the player would get silence forever.
        panecache::Cache c;
        c.SetPanes(std::vector<u32>());
        c.NotePollResult(0);
        test::Check(c.NeedsScan(), "an empty cache keeps asking");
    }

    void TestInvalidate(void)
    {
        test::Section("explicit invalidation");

        // Entering a battle replaces every pane at once; waiting out the
        // interval would narrate the old screen.
        panecache::Cache c;
        c.SetPanes(MakePanes(10));
        c.NotePollResult(10);
        test::Check(!c.NeedsScan(), "quiet normally");

        c.Invalidate();
        test::Check(c.NeedsScan(), "invalidation forces a scan at once");
    }

    void TestScanClearsForced(void)
    {
        test::Section("scanning clears the request");

        panecache::Cache c;
        c.Invalidate();
        test::Check(c.NeedsScan(), "asked for");
        c.SetPanes(MakePanes(3));
        test::Check(!c.NeedsScan(), "and satisfied by scanning");
    }
}

int main(void)
{
    std::printf("\npane cache policy\n=================\n");

    TestScansFirst();
    TestRescanInterval();
    TestStaleCacheForcesRescan();
    TestChurnDoesNotForceRescan();
    TestHalfFailingIsStale();
    TestEmptyResultForcesRescan();
    TestInvalidate();
    TestScanClearsForced();

    return test::Report("panecache");
}
