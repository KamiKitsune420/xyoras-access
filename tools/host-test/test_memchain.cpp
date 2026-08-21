/*
 * XYORAS Access — host tests for pointer-chain walking and range guards.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * This is the code that stands between a stale pointer and a crash, and on
 * hardware its failure cases are exactly the ones you cannot summon on demand.
 * Here the address space is a fake we control, so every one of them is
 * reachable: unmapped reads, pointers out of the heap, chains that go bad
 * halfway, offsets that would wrap the address space.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/memchain.hpp"

#include <map>

using namespace xyoras;

namespace {

    /// A fake address space. Only addresses explicitly written are readable;
    /// everything else fails the way an unmapped page would.
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
            return false;       // unmapped
        out = it->second;
        return true;
    }

    void TestInHeap(void)
    {
        test::Section("heap window");

        test::Check(mem::InHeap(0x08000000), "lower bound is inside");
        test::Check(mem::InHeap(0x08800000), "a typical address is inside");
        test::Check(!mem::InHeap(0x08DF0000), "upper bound is exclusive");
        test::Check(!mem::InHeap(0x07FFFFFF), "just below is outside");
        test::Check(!mem::InHeap(0), "null is outside");
        test::Check(!mem::InHeap(0xFFFFFFFF), "0xFFFFFFFF is outside");
        test::Check(!mem::InHeap(0x14000000), "linear memory is outside the game heap");
    }

    void TestInHeapRange(void)
    {
        test::Section("range fits in the heap");

        test::Check(mem::InHeapRange(0x08000000, 0x100), "a small object at the base fits");
        test::Check(mem::InHeapRange(0x08DEFF00, 0x100), "an object ending exactly at the top fits");
        test::Check(!mem::InHeapRange(0x08DEFF00, 0x101), "one byte past the top does not");
        test::Check(!mem::InHeapRange(0x08000000, 0), "a zero-size read is rejected");
        test::Check(!mem::InHeapRange(0x07000000, 0x10), "an object below the heap is rejected");

        // The check subtracts rather than adds, so a huge size cannot wrap the
        // sum back into the valid window and slip through.
        test::Check(!mem::InHeapRange(0x08800000, 0xFFFFFFFFu),
                    "a size near 2^32 cannot wrap past the guard");
        test::Check(!mem::InHeapRange(0x08800000, 0xF8000000u),
                    "a size that would wrap to a low address is rejected");
    }

    void TestReadPointer(void)
    {
        test::Section("reading a pointer");

        FakeMemory m;
        m.Put(0x08100000, 0x08200000);   // valid: points into the heap
        m.Put(0x08100004, 0x00000000);   // null
        m.Put(0x08100008, 0x14000000);   // outside the heap

        u32 out = 0;
        test::Check(mem::ReadPointer(FakeRead, &m, 0x08100000, out), "a heap pointer is accepted");
        test::Equal(out, 0x08200000u, "and its value is returned");

        test::Check(!mem::ReadPointer(FakeRead, &m, 0x08100004, out), "a null pointer is rejected");
        test::Check(!mem::ReadPointer(FakeRead, &m, 0x08100008, out),
                    "a pointer outside the heap is rejected");
        test::Check(!mem::ReadPointer(FakeRead, &m, 0x08999999, out),
                    "an unmapped source address is rejected");
        test::Check(!mem::ReadPointer(FakeRead, &m, 0x00001000, out),
                    "a source address outside the heap is rejected");
        test::Check(!mem::ReadPointer(nullptr, &m, 0x08100000, out), "a null reader is rejected");
    }

    void TestWalkChainHappyPath(void)
    {
        test::Section("walking a valid chain");

        // base -> 0x08200000, +0x10 -> read 0x08200010 -> 0x08300000, +0x24
        // final = 0x08300024
        FakeMemory m;
        m.Put(0x08100000, 0x08200000);
        m.Put(0x08200010, 0x08300000);

        const u32 offsets[] = {0x10, 0x24};
        u32 out = 0;

        test::Check(mem::WalkChain(FakeRead, &m, 0x08100000, offsets, 2, out),
                    "a two-link chain resolves");
        test::Equal(out, 0x08300024u, "final address is correct");
        test::Equal(m.reads, 2u, "exactly one read per link");
    }

    void TestWalkChainZeroLength(void)
    {
        test::Section("chain with no links");

        FakeMemory m;
        u32 out = 0;

        test::Check(mem::WalkChain(FakeRead, &m, 0x08100000, nullptr, 0, out),
                    "zero links validates the base and returns it");
        test::Equal(out, 0x08100000u, "base is returned unchanged");
        test::Equal(m.reads, 0u, "no reads performed");

        test::Check(!mem::WalkChain(FakeRead, &m, 0x00000000, nullptr, 0, out),
                    "a base outside the heap is still rejected");
    }

    void TestWalkChainBreaks(void)
    {
        test::Section("chains that go bad");

        FakeMemory m;
        m.Put(0x08100000, 0x08200000);
        // 0x08200010 is deliberately absent: the second link is unmapped.

        const u32 offsets[] = {0x10, 0x24};
        u32 out = 0xDEADBEEF;

        test::Check(!mem::WalkChain(FakeRead, &m, 0x08100000, offsets, 2, out),
                    "an unmapped second link fails");
        test::Equal(out, 0xDEADBEEFu, "the output is left untouched on failure");

        // A link that reads successfully but points nowhere sensible.
        FakeMemory bad;
        bad.Put(0x08100000, 0x08200000);
        bad.Put(0x08200010, 0x00000000);
        test::Check(!mem::WalkChain(FakeRead, &bad, 0x08100000, offsets, 2, out),
                    "a null intermediate pointer fails");

        FakeMemory outside;
        outside.Put(0x08100000, 0x08200000);
        outside.Put(0x08200010, 0xFFFF0000);
        test::Check(!mem::WalkChain(FakeRead, &outside, 0x08100000, offsets, 2, out),
                    "an intermediate pointer outside the heap fails");
    }

    void TestWalkChainOffsetOverflow(void)
    {
        test::Section("offsets that would wrap");

        FakeMemory m;
        m.Put(0x08100000, 0x08800000);

        // An offset large enough to wrap past the end of the address space
        // must be caught before it produces a plausible-looking low address.
        const u32 huge[] = {0xFFFFFFFFu};
        u32 out = 0;
        test::Check(!mem::WalkChain(FakeRead, &m, 0x08100000, huge, 1, out),
                    "an offset that would wrap is rejected");

        // A merely large offset that lands outside the heap is also rejected,
        // without wrapping.
        const u32 big[] = {0x10000000u};
        test::Check(!mem::WalkChain(FakeRead, &m, 0x08100000, big, 1, out),
                    "an offset landing outside the heap is rejected");
    }

    void TestWalkChainNullOffsets(void)
    {
        test::Section("argument validation");

        FakeMemory m;
        m.Put(0x08100000, 0x08200000);
        u32 out = 0;

        test::Check(!mem::WalkChain(FakeRead, &m, 0x08100000, nullptr, 2, out),
                    "a non-zero count with null offsets is rejected");
        test::Check(!mem::WalkChain(nullptr, &m, 0x08100000, nullptr, 0, out),
                    "a null reader is rejected even with no links");
    }

    void TestDeepChain(void)
    {
        test::Section("a long chain");

        // Five links, the sort of depth real Gen 6 structures need.
        FakeMemory m;
        m.Put(0x08100000, 0x08200000);
        m.Put(0x08200008, 0x08300000);
        m.Put(0x08300010, 0x08400000);
        m.Put(0x08400018, 0x08500000);
        m.Put(0x08500020, 0x08600000);

        const u32 offsets[] = {0x08, 0x10, 0x18, 0x20, 0x28};
        u32 out = 0;

        test::Check(mem::WalkChain(FakeRead, &m, 0x08100000, offsets, 5, out),
                    "a five-link chain resolves");
        test::Equal(out, 0x08600028u, "final address is correct");
        test::Equal(m.reads, 5u, "one read per link, no re-reads");
    }
}

int main(void)
{
    std::printf("\npointer chains and guards\n=========================\n");

    TestInHeap();
    TestInHeapRange();
    TestReadPointer();
    TestWalkChainHappyPath();
    TestWalkChainZeroLength();
    TestWalkChainBreaks();
    TestWalkChainOffsetOverflow();
    TestWalkChainNullOffsets();
    TestDeepChain();

    return test::Report("memchain");
}
