/*
 * XYORAS Access — host tests for reading app::tool::MenuWindow.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The layout under test came from the constructor at 0x00331200 (see
 * AI docks/12-research-log.md). These tests pin it down so a future change
 * cannot quietly break menu narration, which has no other alarm: the failure
 * mode is silence, and silence is what it sounded like before.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/menuwindow.hpp"

#include <cstdio>
#include <map>

using namespace xyoras;

namespace {

    /// Stand-in heap. Only addresses written here read back.
    std::map<u32, u32> g_mem;

    bool FakeRead32(u32 address, u32 &out, void *)
    {
        std::map<u32, u32>::const_iterator it = g_mem.find(address);
        if (it == g_mem.end())
            return false;
        out = it->second;
        return true;
    }

    /// Lays out a menu exactly as the constructor does.
    void MakeMenu(u32 object, u32 dimA, u32 dimB, u32 items, u32 selected)
    {
        g_mem.clear();
        g_mem[object + menuwindow::kDimA]      = dimA;
        g_mem[object + menuwindow::kDimB]      = dimB;
        g_mem[object + menuwindow::kItemArray] = items;
        g_mem[object + menuwindow::kSelected]  = selected;
    }

    const u32 kObj   = 0x083FB090;   // the object actually seen in game
    const u32 kItems = 0x08231544;

    void TestTheRealMenu(void)
    {
        test::Section("the YES/NO menu that was captured in game");

        // dimA=2, dimB=1, selected=1 -- read off a live console.
        MakeMenu(kObj, 2, 1, kItems, 1);

        menuwindow::State st;
        test::Check(menuwindow::Read(FakeRead32, 0, kObj, st), "it reads");
        test::Equal(st.count, 2u, "two options");
        test::Equal(st.selected, 1u, "the second is focused");
        test::Check(st.HasFocus(), "so there is a focus");
    }

    void TestGridMenu(void)
    {
        test::Section("a grid menu");

        // Count is the product: menus are laid out as grids even when they
        // look like a single column.
        MakeMenu(kObj, 3, 4, kItems, 5);

        menuwindow::State st;
        test::Check(menuwindow::Read(FakeRead32, 0, kObj, st), "it reads");
        test::Equal(st.count, 12u, "three by four is twelve options");
        test::Equal(st.selected, 5u, "and the sixth is focused");
    }

    void TestNothingSelected(void)
    {
        test::Section("a menu with the cursor nowhere");

        // The constructor writes 0xFFFFFFFF, so this is seen whenever a menu
        // exists but has not been landed on yet. Announcing an item then would
        // read whatever happened to be at index -1.
        MakeMenu(kObj, 2, 1, kItems, menuwindow::kNoSelection);

        menuwindow::State st;
        test::Check(menuwindow::Read(FakeRead32, 0, kObj, st), "it still reads");
        test::Check(!st.HasFocus(), "but nothing is focused");
    }

    void TestSelectionPastTheEnd(void)
    {
        test::Section("a selection past the end");

        MakeMenu(kObj, 2, 1, kItems, 7);

        menuwindow::State st;
        test::Check(menuwindow::Read(FakeRead32, 0, kObj, st), "it reads");
        test::Check(!st.HasFocus(), "an out-of-range index is not a focus");
    }

    void TestRubbish(void)
    {
        test::Section("addresses that are not menus");

        menuwindow::State st;

        // A freed object, or simply the wrong address. Believing a wild product
        // here would walk the heap looking for entries.
        MakeMenu(kObj, 0x4000, 0x4000, kItems, 0);
        test::Check(!menuwindow::Read(FakeRead32, 0, kObj, st), "an absurd size is rejected");

        MakeMenu(kObj, 0, 0, kItems, 0);
        test::Check(!menuwindow::Read(FakeRead32, 0, kObj, st), "a zero size is rejected");

        MakeMenu(kObj, 2, 1, 0x00001000, 0);
        test::Check(!menuwindow::Read(FakeRead32, 0, kObj, st),
                    "an item array outside the heap is rejected");

        g_mem.clear();
        test::Check(!menuwindow::Read(FakeRead32, 0, kObj, st), "unreadable memory is rejected");
    }

    void TestEntryAddresses(void)
    {
        test::Section("where the entries are");

        MakeMenu(kObj, 4, 1, kItems, 2);

        menuwindow::State st;
        menuwindow::Read(FakeRead32, 0, kObj, st);

        // Stride 0x1C, payload 0x14 into each entry.
        test::Equal(menuwindow::EntryPayload(st, 0), kItems + 0x14, "first entry");
        test::Equal(menuwindow::EntryPayload(st, 1), kItems + 0x1C + 0x14, "second entry");
        test::Equal(menuwindow::EntryPayload(st, 3), kItems + 3 * 0x1C + 0x14, "last entry");
        test::Equal(menuwindow::EntryPayload(st, 4), 0u, "one past the end is nothing");
        test::Equal(menuwindow::EntryPayload(st, 99), 0u, "far past the end is nothing");
    }
}

int main(void)
{
    printf("\nreading the menu cursor\n======================\n");

    TestTheRealMenu();
    TestGridMenu();
    TestNothingSelected();
    TestSelectionPastTheEnd();
    TestRubbish();
    TestEntryAddresses();

    return test::Report("menuwindow");
}
