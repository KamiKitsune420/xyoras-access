/*
 * XYORAS Access — host tests for the generated name tables.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * These tables are generated, so the risk is not bad code but a bad index: an
 * off-by-one shifts every name by one and the mod confidently calls Pikachu
 * "Raichu" forever. The spot checks below pin known indices across the whole
 * range of each table.
 *
 * They also check the bounds guard, because indices come from game memory and
 * a bad read must produce "unknown" rather than walk off the end of an array.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/names.hpp"

#include <cstring>

using namespace xyoras;

namespace {

    void TestSpecies(void)
    {
        test::Section("species names");

        // Spread across the table: first, each generation's starter or
        // landmark, and the very last valid index.
        test::EqualStr(names::Species(1), "Bulbasaur", "1 Bulbasaur");
        test::EqualStr(names::Species(25), "Pikachu", "25 Pikachu");
        test::EqualStr(names::Species(150), "Mewtwo", "150 Mewtwo");
        test::EqualStr(names::Species(151), "Mew", "151 Mew");
        test::EqualStr(names::Species(152), "Chikorita", "152 Chikorita (Gen 2 boundary)");
        test::EqualStr(names::Species(252), "Treecko", "252 Treecko (Gen 3 boundary)");
        test::EqualStr(names::Species(387), "Turtwig", "387 Turtwig (Gen 4 boundary)");
        test::EqualStr(names::Species(494), "Victini", "494 Victini (Gen 5 boundary)");
        test::EqualStr(names::Species(650), "Chespin", "650 Chespin (Gen 6 boundary)");
        test::EqualStr(names::Species(716), "Xerneas", "716 Xerneas");
        test::EqualStr(names::Species(717), "Yveltal", "717 Yveltal");
        test::EqualStr(names::Species(721), "Volcanion", "721 Volcanion (last in Gen 6)");
    }

    void TestMoves(void)
    {
        test::Section("move names");

        test::EqualStr(names::Move(1), "Pound", "1 Pound");
        test::EqualStr(names::Move(84), "Thunder Shock", "84 Thunder Shock");
        test::EqualStr(names::Move(85), "Thunderbolt", "85 Thunderbolt");
        test::EqualStr(names::Move(87), "Thunder", "87 Thunder");
        test::EqualStr(names::Move(165), "Struggle", "165 Struggle");
        // 618-621 are the ORAS signature moves, and 622 in the untrimmed
        // source is Breakneck Blitz -- a Gen 7 Z-move. That makes this
        // boundary verifiable by content, not just by counting.
        test::EqualStr(names::Move(618), "Origin Pulse", "618 Origin Pulse");
        test::EqualStr(names::Move(620), "Dragon Ascent", "620 Dragon Ascent");
        test::EqualStr(names::Move(621), "Hyperspace Fury", "621 Hyperspace Fury (last in Gen 6)");
    }

    void TestAbilities(void)
    {
        test::Section("ability names");

        test::EqualStr(names::Ability(1), "Stench", "1 Stench");
        test::EqualStr(names::Ability(9), "Static", "9 Static");
        test::EqualStr(names::Ability(191), "Delta Stream", "191 Delta Stream (last in Gen 6)");
    }

    void TestItems(void)
    {
        test::Section("item names");

        test::EqualStr(names::Item(1), "Master Ball", "1 Master Ball");
        test::EqualStr(names::Item(2), "Ultra Ball", "2 Ultra Ball");
        test::EqualStr(names::Item(4), "Poke Ball", "4 Poke Ball (accent stripped)");
        test::EqualStr(names::Item(17), "Potion", "17 Potion");
    }

    void TestBounds(void)
    {
        test::Section("bounds guard");

        // Indices come from game memory. A bad read must not index off the end.
        test::EqualStr(names::Species(722), "unknown", "species past the end");
        test::EqualStr(names::Species(9999), "unknown", "species wildly out of range");
        test::EqualStr(names::Species(0xFFFF), "unknown", "species 0xFFFF (a common junk read)");
        test::EqualStr(names::Move(622), "unknown", "move past the end");
        test::EqualStr(names::Ability(192), "unknown", "ability past the end");
        test::EqualStr(names::Item(776), "unknown", "item past the end");
    }

    void TestNoGen7Leakage(void)
    {
        test::Section("trimmed to Generation 6");

        // Rowlet is species 722. If it is reachable, the table was not
        // trimmed and the plugin carries data for games it does not support.
        test::Equal(names::kSpeciesCount, 722u, "species table stops after 721");
        test::Equal(names::kMovesCount, 622u, "move table stops after 621");
        test::Equal(names::kAbilitiesCount, 192u, "ability table stops after 191");
        test::Equal(names::kItemsCount, 776u, "item table stops after 775");
    }

    void TestNoEmptyOrNullEntries(void)
    {
        test::Section("table integrity");

        // A null or empty entry would be spoken as silence, which reads to the
        // player as the mod having failed rather than as a gap in the data.
        u32 emptySpecies = 0;
        for (u32 i = 1; i < names::kSpeciesCount; ++i)
            if (names::kSpecies[i] == nullptr || names::kSpecies[i][0] == '\0')
                ++emptySpecies;
        test::Equal(emptySpecies, 0u, "no empty species entries");

        u32 emptyMoves = 0;
        for (u32 i = 1; i < names::kMovesCount; ++i)
            if (names::kMoves[i] == nullptr || names::kMoves[i][0] == '\0')
                ++emptyMoves;
        test::Equal(emptyMoves, 0u, "no empty move entries");

        // Every name must be plain ASCII: eSpeak is fed these directly, and a
        // stray high byte is spoken as noise.
        u32 nonAscii = 0;
        for (u32 i = 0; i < names::kSpeciesCount; ++i)
            for (const char *p = names::kSpecies[i]; *p; ++p)
                if (static_cast<unsigned char>(*p) > 126)
                    ++nonAscii;
        for (u32 i = 0; i < names::kItemsCount; ++i)
            for (const char *p = names::kItems[i]; *p; ++p)
                if (static_cast<unsigned char>(*p) > 126)
                    ++nonAscii;
        test::Equal(nonAscii, 0u, "all names are ASCII");
    }
}

int main(void)
{
    std::printf("\nname tables\n===========\n");

    TestSpecies();
    TestMoves();
    TestAbilities();
    TestItems();
    TestBounds();
    TestNoGen7Leakage();
    TestNoEmptyOrNullEntries();

    return test::Report("names");
}
