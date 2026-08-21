/*
 * XYORAS Access — host tests for vtable-based object discovery.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The scan runs over the whole game heap on a console, so its behaviour around
 * unmapped pages and bad inputs matters: aborting at the first unreadable word
 * would make it find nothing, and accepting a nonsense vtable would make it
 * "find" hundreds of meaningless hits and send the search off in the wrong
 * direction entirely.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/vtscan.hpp"

#include <map>

using namespace xyoras;

namespace {

    struct FakeMemory
    {
        std::map<u32, u32> words;
        u32 reads = 0;
        void Put(u32 address, u32 value) { words[address] = value; }
    };

    bool FakeRead(u32 address, u32 &out, void *ctx)
    {
        FakeMemory *m = static_cast<FakeMemory *>(ctx);
        ++m->reads;
        std::map<u32, u32>::const_iterator it = m->words.find(address);
        if (it == m->words.end())
            return false;
        out = it->second;
        return true;
    }

    // A plausible vtable address, matching the shape found for
    // app::tool::TalkWindow.
    const u32 kTalkWindowVtable = 0x005970FC;

    void TestPlausibleVtable(void)
    {
        test::Section("what looks like a vtable");

        test::Check(vtscan::PlausibleVtable(0x005970FC), "an address inside code.bin");
        test::Check(vtscan::PlausibleVtable(0x00100000), "the code base itself");
        test::Check(!vtscan::PlausibleVtable(0x000FFFFC), "below the code base");
        test::Check(!vtscan::PlausibleVtable(0x00700000), "at the upper limit (exclusive)");
        test::Check(!vtscan::PlausibleVtable(0x08800000), "a heap address is not a vtable");
        test::Check(!vtscan::PlausibleVtable(0), "null");

        // Vtables are word-aligned. An unaligned value is a sign the address
        // table has a typo, and scanning for it would waste a whole pass.
        test::Check(!vtscan::PlausibleVtable(0x005970FD), "an unaligned address is rejected");
        test::Check(!vtscan::PlausibleVtable(0x005970FE), "another unaligned address");
    }

    void TestFindsObjects(void)
    {
        test::Section("finding objects");

        FakeMemory m;
        m.Put(0x08100000, kTalkWindowVtable);   // an object
        m.Put(0x08100004, 0x12345678);          // its members
        m.Put(0x08200000, kTalkWindowVtable);   // a second object
        m.Put(0x08300000, 0x00597100);          // a different class

        u32 hits[8] = {0};
        const u32 n = vtscan::FindObjects(FakeRead, &m, 0x08100000, 0x08300010,
                                          kTalkWindowVtable, hits, 8);

        test::Equal(n, 2u, "both instances found");
        test::Equal(hits[0], 0x08100000u, "first at the object address, not the vtable");
        test::Equal(hits[1], 0x08200000u, "second likewise");
    }

    void TestUnmappedPagesDoNotStopTheScan(void)
    {
        test::Section("unmapped pages");

        // Most of the heap is unreadable at any given moment. A scan that gave
        // up at the first failed read would find nothing at all.
        FakeMemory m;
        m.Put(0x08100000, 0x11111111);
        m.Put(0x08500000, kTalkWindowVtable);   // far past a large unmapped gap

        u32 hits[4] = {0};
        const u32 n = vtscan::FindObjects(FakeRead, &m, 0x08100000, 0x08500010,
                                          kTalkWindowVtable, hits, 4);

        test::Equal(n, 1u, "the match beyond the gap is still found");
        test::Equal(hits[0], 0x08500000u, "at the right address");
    }

    void TestReportsTrueTotal(void)
    {
        test::Section("total count versus recorded hits");

        // "Exactly one instance" and "hundreds" mean very different things
        // about whether the vtable address is right, so the true total must be
        // reported even when the caller's buffer is smaller.
        FakeMemory m;
        for (u32 i = 0; i < 10; ++i)
            m.Put(0x08100000 + i * 0x1000, kTalkWindowVtable);

        u32 hits[3] = {0};
        const u32 n = vtscan::FindObjects(FakeRead, &m, 0x08100000, 0x0810A000,
                                          kTalkWindowVtable, hits, 3);

        test::Equal(n, 10u, "the true total is returned");
        test::Equal(hits[0], 0x08100000u, "the first hits are still recorded");
        test::Equal(hits[2], 0x08102000u, "up to the buffer size");
    }

    void TestRejectsBadInput(void)
    {
        test::Section("bad input");

        FakeMemory m;
        m.Put(0x08100000, kTalkWindowVtable);
        u32 hits[4] = {0};

        test::Equal(vtscan::FindObjects(nullptr, &m, 0x08100000, 0x08100010,
                                        kTalkWindowVtable, hits, 4), 0u,
                    "a null reader finds nothing");
        test::Equal(vtscan::FindObjects(FakeRead, &m, 0x08100000, 0x08100010,
                                        0x08800000, hits, 4), 0u,
                    "a heap address as the target is refused");
        test::Equal(vtscan::FindObjects(FakeRead, &m, 0x08100010, 0x08100000,
                                        kTalkWindowVtable, hits, 4), 0u,
                    "an inverted range finds nothing");
        test::Equal(vtscan::FindObjects(FakeRead, &m, 0x08100001, 0x08100010,
                                        kTalkWindowVtable, hits, 4), 0u,
                    "an unaligned start is refused");
    }

    void TestNullHitBuffer(void)
    {
        test::Section("counting without recording");

        // Counting alone is the common case when confirming an address.
        FakeMemory m;
        m.Put(0x08100000, kTalkWindowVtable);
        m.Put(0x08100008, kTalkWindowVtable);

        test::Equal(vtscan::FindObjects(FakeRead, &m, 0x08100000, 0x08100010,
                                        kTalkWindowVtable, nullptr, 0), 2u,
                    "a null buffer still counts correctly");
    }

    void TestReadVtable(void)
    {
        test::Section("identifying an object");

        FakeMemory m;
        m.Put(0x08100000, kTalkWindowVtable);   // a real object
        m.Put(0x08200000, 0x00000000);          // zeroed memory
        m.Put(0x08300000, 0x08400000);          // a plain pointer, not a vtable

        u32 vt = 0;
        test::Check(vtscan::ReadVtable(FakeRead, &m, 0x08100000, vt), "an object is recognised");
        test::Equal(vt, kTalkWindowVtable, "and its vtable returned");

        test::Check(!vtscan::ReadVtable(FakeRead, &m, 0x08200000, vt),
                    "zeroed memory is not an object");
        test::Check(!vtscan::ReadVtable(FakeRead, &m, 0x08300000, vt),
                    "a heap pointer is not a vtable");
        test::Check(!vtscan::ReadVtable(FakeRead, &m, 0x08999999, vt),
                    "an unmapped address is not an object");
        test::Check(!vtscan::ReadVtable(FakeRead, &m, 0x00100000, vt),
                    "an address outside the heap is refused");
    }

    void TestHeapWrapper(void)
    {
        test::Section("scanning the whole heap");

        FakeMemory m;
        m.Put(mem::kHeapMin, kTalkWindowVtable);

        // Only the mapped word is readable, so this also checks the wrapper
        // survives a heap that is almost entirely unmapped.
        u32 hits[2] = {0};
        const u32 n = vtscan::FindObjectsInHeap(FakeRead, &m, kTalkWindowVtable, hits, 2);

        test::Equal(n, 1u, "the single object in the heap is found");
        test::Equal(hits[0], mem::kHeapMin, "at the heap base");
    }
}

int main(void)
{
    std::printf("\nvtable object discovery\n=======================\n");

    TestPlausibleVtable();
    TestFindsObjects();
    TestUnmappedPagesDoNotStopTheScan();
    TestReportsTrueTotal();
    TestRejectsBadInput();
    TestNullHitBuffer();
    TestReadVtable();
    TestHeapWrapper();

    return test::Report("vtscan");
}
