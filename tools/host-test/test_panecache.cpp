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

        c.SetPanes(MakePanes(10), false);
        test::Check(!c.NeedsScan(), "after scanning it does not ask again");
        test::Equal(c.Count(), 10u, "and holds the panes");
    }

    void TestRescanInterval(void)
    {
        test::Section("routine rescans");

        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);

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
        c.SetPanes(MakePanes(10), false);

        c.NotePollResult(2);   // 8 of 10 failed
        test::Check(c.NeedsScan(), "mostly-failing reads force a rescan immediately");
    }

    void TestChurnDoesNotForceRescan(void)
    {
        test::Section("ordinary churn");

        // A pane or two disappearing is normal and must not cost a full scan
        // every poll.
        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);

        c.NotePollResult(9);
        test::Check(!c.NeedsScan(), "one failure is tolerated");
        c.NotePollResult(8);
        test::Check(!c.NeedsScan(), "two failures are tolerated");
    }

    void TestHalfFailingIsStale(void)
    {
        test::Section("the staleness threshold");

        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);
        c.NotePollResult(5);        // exactly half failed
        test::Check(c.NeedsScan(), "half failing counts as stale");

        panecache::Cache c2;
        c2.SetPanes(MakePanes(10), false);
        c2.NotePollResult(6);       // 4 of 10 failed
        test::Check(!c2.NeedsScan(), "just under half does not");
    }

    void TestEmptyResultForcesRescan(void)
    {
        test::Section("finding nothing");

        // A scan that returns nothing must not settle into never scanning
        // again -- the player would get silence forever. But it must not scan
        // on every poll either: a full scan costs hundreds of milliseconds,
        // and back-to-back scans through a whole transition would be worse
        // than the problem.
        panecache::Cache c;
        c.SetPanes(std::vector<u32>(), false);
        c.NotePollResult(0);
        test::Check(!c.NeedsScan(), "an empty result does not scan again at once");

        for (u32 i = 1; i < panecache::kRescanInterval; ++i)
            c.NotePollResult(0);
        test::Check(c.NeedsScan(), "but it keeps asking, once the interval elapses");
    }

    void TestInvalidate(void)
    {
        test::Section("explicit invalidation");

        // Entering a battle replaces every pane at once; waiting out the
        // interval would narrate the old screen.
        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);
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
        c.SetPanes(MakePanes(3), false);
        test::Check(!c.NeedsScan(), "and satisfied by scanning");
    }
    void TestFirstScanIsFull(void)
    {
        test::Section("the first scan has nowhere to narrow to");

        panecache::Cache c;
        u32 start = 0, end = 0;
        test::Check(!c.NarrowScanRange(start, end), "nothing learned yet, so scan it all");
        test::Check(!c.HaveWindow(), "and no window is held");
    }

    void TestLearnsWhereThePanesWere(void)
    {
        test::Section("learning where the panes live");

        // The measured cost of a full scan -- 551 ms under emulation -- is why
        // this exists. Panes cluster, so the next scan should not walk the
        // whole heap to find them again.
        panecache::Cache c;
        std::vector<u32> panes;
        panes.push_back(0x08400000);
        panes.push_back(0x08401000);
        panes.push_back(0x08402000);
        c.SetPanes(panes, false);

        u32 start = 0, end = 0;
        test::Check(c.NarrowScanRange(start, end), "a window is offered");
        test::Check(start <= 0x08400000, "it starts at or below the lowest pane");
        test::Check(end   >= 0x08402000, "and ends at or above the highest");
        test::Equal(start & 0xFFFu, 0u, "and starts on a page boundary");

        // Far smaller than the heap, which is the entire point.
        const u32 heapSize = mem::kHeapMax - mem::kHeapMin;
        test::Check((end - start) < heapSize / 10,
                    "and covers a small fraction of the heap");
    }

    void TestWindowIsPadded(void)
    {
        test::Section("slack around the window");

        // A new pane allocated just beside the known ones must still be found
        // without paying for a full scan.
        panecache::Cache c;
        std::vector<u32> panes;
        panes.push_back(0x08400000);
        c.SetPanes(panes, false);

        u32 start = 0, end = 0;
        c.NarrowScanRange(start, end);
        test::Check(start < 0x08400000, "there is room below");
        test::Check(end > 0x08400000, "and above");
    }

    void TestWindowClampsToTheHeap(void)
    {
        test::Section("a pane at the very edge of the heap");

        // Padding must not wrap past the ends and produce a nonsense range.
        panecache::Cache c;
        std::vector<u32> panes;
        panes.push_back(mem::kHeapMin);
        panes.push_back(mem::kHeapMax - 4);
        c.SetPanes(panes, false);

        u32 start = 0, end = 0;
        c.NarrowScanRange(start, end);
        test::Check(start >= mem::kHeapMin, "does not start below the heap");
        test::Check(end <= mem::kHeapMax, "does not end above it");
        test::Check(start < end, "and is not inverted");
    }

    void TestFullScanForcedPeriodically(void)
    {
        test::Section("going wide again now and then");

        // A narrow scan cannot see panes allocated outside its window. Without
        // this the mod would go permanently blind to part of the heap.
        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);

        u32 start = 0, end = 0;
        for (u32 i = 0; i < panecache::kNarrowScansBeforeFull; ++i)
        {
            test::Check(c.NarrowScanRange(start, end),
                        i == 0 ? "narrow scans are offered" : "");
            c.SetPanes(MakePanes(10), true);
        }

        test::Check(!c.NarrowScanRange(start, end),
                    "after enough narrow scans, a full one is required");
    }

    void TestFullScanResetsTheNarrowCount(void)
    {
        test::Section("a full scan starts the count over");

        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);

        u32 start = 0, end = 0;
        for (u32 i = 0; i < panecache::kNarrowScansBeforeFull; ++i)
            c.SetPanes(MakePanes(10), true);
        test::Check(!c.NarrowScanRange(start, end), "a full scan is due");

        c.SetPanes(MakePanes(10), false);
        test::Check(c.NarrowScanRange(start, end), "and narrow scans resume after it");
    }

    void TestEmptyNarrowScanGoesWide(void)
    {
        test::Section("a narrow scan that finds nothing");

        // It proves only that the panes are not where they were. Treating it
        // as "no text on screen" would leave the player in silence on a screen
        // whose panes moved.
        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);
        test::Check(c.HaveWindow(), "a window is held");

        c.SetPanes(std::vector<u32>(), true);

        u32 start = 0, end = 0;
        test::Check(!c.NarrowScanRange(start, end), "the window is dropped");
        test::Check(c.NeedsScan(), "and a scan is asked for at once");
    }

    void TestEmptyFullScanKeepsTrying(void)
    {
        test::Section("a full scan that finds nothing");

        // Normal during a transition. It must keep asking, but it has learned
        // nothing, so there is no window to offer.
        panecache::Cache c;
        c.SetPanes(std::vector<u32>(), false);

        u32 start = 0, end = 0;
        test::Check(!c.NarrowScanRange(start, end), "there is nothing to narrow to");

        // It waits out the interval rather than scanning again immediately.
        test::Check(!c.NeedsScan(), "and does not scan again at once");
        for (u32 i = 0; i < panecache::kRescanInterval; ++i)
            c.NotePollResult(0);
        test::Check(c.NeedsScan(), "but does try again after the interval");
    }

    void TestInvalidateKeepsTheWindow(void)
    {
        test::Section("a context change keeps the window");

        // A new screen allocates its panes from the same heap. Throwing the
        // window away would mean a 551 ms scan at exactly the moment the
        // player is waiting to hear what changed.
        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);
        c.Invalidate();

        u32 start = 0, end = 0;
        test::Check(c.NeedsScan(), "a scan is due");
        test::Check(c.NarrowScanRange(start, end), "but it can still be a narrow one");
    }

    void TestForgetWindow(void)
    {
        test::Section("distrusting the window entirely");

        panecache::Cache c;
        c.SetPanes(MakePanes(10), false);
        c.ForgetWindow();

        u32 start = 0, end = 0;
        test::Check(!c.NarrowScanRange(start, end), "the next scan covers everything");
        test::Check(c.NeedsScan(), "and happens at once");
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

    TestFirstScanIsFull();
    TestLearnsWhereThePanesWere();
    TestWindowIsPadded();
    TestWindowClampsToTheHeap();
    TestFullScanForcedPeriodically();
    TestFullScanResetsTheNarrowCount();
    TestEmptyNarrowScanGoesWide();
    TestEmptyFullScanKeepsTrying();
    TestInvalidateKeepsTheWindow();
    TestForgetWindow();

    return test::Report("panecache");
}
