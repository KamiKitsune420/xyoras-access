/*
 * XYORAS Access — host tests for the spoken wording.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * For a blind player the wording is the interface, so these tests treat
 * phrasing as behaviour rather than cosmetics. They pin the exact sentences,
 * which means a careless edit that changes what the player hears fails loudly
 * instead of shipping.
 *
 * The cases that matter most are the ones a sighted developer would not
 * notice: a sliver of HP that must not round to "0 percent", a fainted Pokemon
 * that should say "fainted" rather than "0 of 155", and the rule that an
 * opponent's exact HP is never spoken because a sighted player cannot see it
 * either.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/phrases.hpp"

using namespace xyoras;
using phrases::Verbosity;

namespace {

    phrases::PokemonInfo MakePikachu(void)
    {
        phrases::PokemonInfo p;
        p.species   = "Pikachu";
        p.ability   = "Static";
        p.level     = 50;
        p.hpCurrent = 118;
        p.hpMax     = 155;
        p.gender    = 1;
        return p;
    }

    void TestStatusNames(void)
    {
        test::Section("status conditions");

        test::Check(phrases::StatusName(0) == nullptr, "no status yields nothing to say");
        test::EqualStr(phrases::StatusName(0x08), "poisoned", "poison");
        test::EqualStr(phrases::StatusName(0x10), "burned", "burn");
        test::EqualStr(phrases::StatusName(0x20), "frozen", "freeze");
        test::EqualStr(phrases::StatusName(0x40), "paralysed", "paralysis");
        test::EqualStr(phrases::StatusName(0x80), "badly poisoned", "toxic");

        // Sleep is a counter, not a flag, so any non-zero count means asleep.
        test::EqualStr(phrases::StatusName(1), "asleep", "sleep counter of 1");
        test::EqualStr(phrases::StatusName(7), "asleep", "sleep counter of 7");
    }

    void TestHpPercent(void)
    {
        test::Section("HP percentage");

        test::Equal(phrases::HpPercent(155, 155), 100u, "full is 100");
        test::Equal(phrases::HpPercent(0, 155), 0u, "fainted is 0");
        test::Equal(phrases::HpPercent(78, 155), 50u, "half is about 50");

        // A single hit point left must never round away to zero: "0 percent"
        // tells the player they have fainted when they have not.
        test::Equal(phrases::HpPercent(1, 155), 1u, "one HP left reads as 1, not 0");
        test::Equal(phrases::HpPercent(1, 999), 1u, "one HP of many still reads as 1");

        // Nor should nearly-full round up to 100 and hide chip damage.
        test::Equal(phrases::HpPercent(154, 155), 99u, "one HP missing is not 100");
        test::Equal(phrases::HpPercent(10, 0), 0u, "a zero maximum does not divide by zero");
    }

    void TestHpWording(void)
    {
        test::Section("HP wording");

        test::EqualStr(phrases::HpExact(118, 155), "118 of 155", "own HP is exact");
        test::EqualStr(phrases::HpApproximate(78, 155), "50 percent", "opponent HP is a percentage");
        test::EqualStr(phrases::HpApproximate(0, 155), "fainted", "a fainted opponent is named as such");
    }

    void TestPartyMember(void)
    {
        test::Section("party member");

        const phrases::PokemonInfo p = MakePikachu();

        test::EqualStr(phrases::PartyMember(p, 2, 6, Verbosity::Terse),
                       "Pikachu, level 50, 118 of 155.",
                       "terse omits the slot position and the words 'hit points'");

        test::EqualStr(phrases::PartyMember(p, 2, 6, Verbosity::Normal),
                       "2 of 6. Pikachu, level 50, 118 of 155 hit points.",
                       "normal includes the slot position");

        test::EqualStr(phrases::PartyMember(p, 2, 6, Verbosity::Verbose),
                       "2 of 6. Pikachu, level 50, 118 of 155 hit points, ability Static.",
                       "verbose adds the ability");
    }

    void TestPartyMemberStatusAndItem(void)
    {
        test::Section("party member with status and item");

        phrases::PokemonInfo p = MakePikachu();
        p.status = 0x10;              // burned
        p.heldItem = "Light Ball";

        test::EqualStr(phrases::PartyMember(p, 1, 6, Verbosity::Normal),
                       "1 of 6. Pikachu, level 50, 118 of 155 hit points, burned.",
                       "status is spoken at normal verbosity");

        test::EqualStr(phrases::PartyMember(p, 1, 6, Verbosity::Verbose),
                       "1 of 6. Pikachu, level 50, 118 of 155 hit points, burned, "
                       "ability Static, holding Light Ball.",
                       "verbose adds the held item too");
    }

    void TestFaintedPartyMember(void)
    {
        test::Section("a fainted party member");

        phrases::PokemonInfo p = MakePikachu();
        p.hpCurrent = 0;
        p.status = 0x10;

        // "0 of 155" is a number the player has to interpret; "fainted" is the
        // fact they need. And a fainted Pokemon's burn is not worth saying.
        test::EqualStr(phrases::PartyMember(p, 3, 6, Verbosity::Normal),
                       "3 of 6. Pikachu, level 50, fainted.",
                       "says 'fainted' rather than zero HP, and drops the status");
    }

    void TestNicknames(void)
    {
        test::Section("nicknames");

        phrases::PokemonInfo p = MakePikachu();
        p.nickname = "Sparky";

        // The player chose the nickname and knows it, so terse uses it alone.
        test::EqualStr(phrases::Name(p, Verbosity::Terse), "Sparky",
                       "terse uses the nickname alone");
        test::EqualStr(phrases::Name(p, Verbosity::Normal), "Sparky, Pikachu",
                       "normal adds the species so the creature is identifiable");

        p.nickname = "";
        test::EqualStr(phrases::Name(p, Verbosity::Normal), "Pikachu",
                       "an empty nickname is ignored");
        p.nickname = nullptr;
        test::EqualStr(phrases::Name(p, Verbosity::Normal), "Pikachu",
                       "a null nickname is ignored");
    }

    void TestMenuItems(void)
    {
        test::Section("menu entries");

        test::EqualStr(phrases::MenuItem("Potion", 3, 12), "Potion, 3 of 12",
                       "position lets the player place themselves in the list");
        test::EqualStr(phrases::MenuItem("Potion", 1, 0), "Potion",
                       "an unknown list length is simply omitted");
        test::EqualStr(phrases::MenuItem("", 1, 5), "blank, 1 of 5",
                       "an empty entry is announced rather than silent");
        test::EqualStr(phrases::MenuItem(nullptr, 1, 5), "blank, 1 of 5",
                       "a null entry is announced rather than crashing");

        test::EqualStr(phrases::MenuItemWithQuantity("Potion", 5, 3, 12),
                       "Potion, 5, 3 of 12", "bag entries carry a quantity");
    }

    void TestPosition(void)
    {
        test::Section("position report");

        test::EqualStr(phrases::Position("Route 4", 12, 34, 0, Verbosity::Terse),
                       "Route 4.", "terse is just the place");
        test::EqualStr(phrases::Position("Route 4", 12, 34, 0, Verbosity::Normal),
                       "Route 4, facing north.", "normal adds facing");
        test::EqualStr(phrases::Position("Route 4", 12, 34, 1, Verbosity::Verbose),
                       "Route 4, facing east, at 12, 34.",
                       "verbose adds coordinates");
        test::EqualStr(phrases::Position(nullptr, 0, 0, 2, Verbosity::Normal),
                       "unknown area, facing south.",
                       "an unknown map still reports facing");
    }

    void TestFacingDirections(void)
    {
        test::Section("compass directions");

        test::EqualStr(phrases::Facing(static_cast<u8>(0)), "north", "0 is north");
        test::EqualStr(phrases::Facing(static_cast<u8>(1)), "east", "1 is east");
        test::EqualStr(phrases::Facing(static_cast<u8>(2)), "south", "2 is south");
        test::EqualStr(phrases::Facing(static_cast<u8>(3)), "west", "3 is west");
        test::EqualStr(phrases::Facing(static_cast<u8>(7)), "west", "out-of-range wraps safely");
    }

    void TestFacingScan(void)
    {
        test::Section("what is ahead");

        test::EqualStr(phrases::Facing("a wall", Verbosity::Normal), "a wall ahead.",
                       "normal is a sentence");
        test::EqualStr(phrases::Facing("a wall", Verbosity::Terse), "a wall",
                       "terse is just the thing, because this is spoken constantly");
        test::EqualStr(phrases::Facing(nullptr, Verbosity::Terse), "clear",
                       "an empty tile is 'clear', not silence");
    }

    void TestBattleStatus(void)
    {
        test::Section("battle status");

        phrases::PokemonInfo mine = MakePikachu();
        phrases::PokemonInfo theirs;
        theirs.species   = "Gyarados";
        theirs.level     = 55;
        theirs.hpCurrent = 100;
        theirs.hpMax     = 200;

        // The player's own HP is exact; the opponent's is a percentage,
        // because that is all a sighted player is shown. Exact opponent HP
        // would be an advantage, which rule 2 forbids.
        test::EqualStr(phrases::BattleStatus(mine, theirs, Verbosity::Normal),
                       "Pikachu, level 50, 118 of 155. Opponent Gyarados, level 55, 50 percent.",
                       "own HP exact, opponent HP as a percentage");

        mine.status = 0x40;      // paralysed
        theirs.hpCurrent = 0;
        test::EqualStr(phrases::BattleStatus(mine, theirs, Verbosity::Normal),
                       "Pikachu, level 50, 118 of 155, paralysed. "
                       "Opponent Gyarados, level 55, fainted.",
                       "statuses and a fainted opponent");
    }

    void TestMoveEntry(void)
    {
        test::Section("move menu entries");

        test::EqualStr(phrases::MoveEntry("Thunderbolt", 15, 15, 1, 4, Verbosity::Normal),
                       "Thunderbolt, 15 of 15 PP, 1 of 4", "a usable move");

        // An unusable move must be obvious before the player commits to it.
        test::EqualStr(phrases::MoveEntry("Thunderbolt", 0, 15, 1, 4, Verbosity::Normal),
                       "Thunderbolt, no PP left, 1 of 4", "an exhausted move says so plainly");

        test::EqualStr(phrases::MoveEntry("Thunderbolt", 15, 15, 1, 4, Verbosity::Terse),
                       "Thunderbolt, 15 of 15 PP", "terse drops the position");
    }

    void TestNoEmptyUtterances(void)
    {
        test::Section("nothing produces an empty utterance");

        // Silence reads to the player as the mod having broken, so every
        // entry point must produce words even when handed nothing.
        phrases::PokemonInfo blank;
        test::Check(!phrases::PartyMember(blank, 1, 1).empty(), "a blank Pokemon still speaks");
        test::Check(!phrases::MenuItem(nullptr, 0, 0).empty(), "a null menu item still speaks");
        test::Check(!phrases::Position(nullptr, 0, 0, 0).empty(), "an unknown position still speaks");
        test::Check(!phrases::MoveEntry(nullptr, 0, 0, 0, 0).empty(), "a null move still speaks");
    }
}

int main(void)
{
    std::printf("\nspoken wording\n==============\n");

    TestStatusNames();
    TestHpPercent();
    TestHpWording();
    TestPartyMember();
    TestPartyMemberStatusAndItem();
    TestFaintedPartyMember();
    TestNicknames();
    TestMenuItems();
    TestPosition();
    TestFacingDirections();
    TestFacingScan();
    TestBattleStatus();
    TestMoveEntry();
    TestNoEmptyUtterances();

    return test::Report("phrases");
}
