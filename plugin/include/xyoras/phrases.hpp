/*
 * XYORAS Access — the exact words the player hears.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Every utterance is built here and nowhere else. Feature code assembles a
 * structured description of what it found; this turns that into English.
 *
 * Keeping the wording in one place is what makes tone consistent, makes the
 * verbosity setting implementable at all, and leaves a single file to
 * translate later. It also makes the phrasing testable, which matters more
 * than it sounds: for a blind player the wording *is* the interface, and a
 * clumsy phrase is a usability bug exactly as much as a wrong number is.
 *
 * Principles from "AI docks/02-accessibility-design.md":
 *   - terse by default, detail on request
 *   - never speak a raw internal id when a name exists
 *   - say nothing rather than say something empty
 *
 * Header-only and 3DS-free, so the host tests exercise the shipped wording.
 */
#ifndef XYORAS_PHRASES_HPP
#define XYORAS_PHRASES_HPP

#include "xyoras/common.hpp"

#include <string>

namespace xyoras { namespace phrases {

    enum class Verbosity { Terse, Normal, Verbose };

    /// What a feature knows about a Pokemon. Names are already resolved --
    /// this layer never looks anything up, so it stays testable in isolation.
    struct PokemonInfo
    {
        const char *species;
        const char *nickname;    ///< empty or null when not nicknamed
        const char *ability;     ///< optional
        const char *heldItem;    ///< optional
        u8  level;
        u16 hpCurrent;
        u16 hpMax;
        u32 status;              ///< PK6 status condition bitfield
        u8  gender;              ///< 0 male, 1 female, 2 genderless

        PokemonInfo()
            : species("unknown"), nickname(nullptr), ability(nullptr),
              heldItem(nullptr), level(0), hpCurrent(0), hpMax(0),
              status(0), gender(2) {}
    };

    namespace detail {

        inline bool Empty(const char *s) { return s == nullptr || s[0] == '\0'; }

        inline std::string Num(u32 v)
        {
            char buf[12];
            u32 n = v;
            int i = 0;
            if (n == 0) buf[i++] = '0';
            while (n > 0 && i < 11) { buf[i++] = static_cast<char>('0' + (n % 10)); n /= 10; }
            std::string out;
            while (i > 0) out.push_back(buf[--i]);
            return out;
        }
    }

    /// Status conditions, in the words a player would use.
    ///
    /// Sleep is a counter in the low three bits rather than a flag, so it is
    /// checked first -- a sleeping Pokemon has a non-zero count there and no
    /// other bit set.
    inline const char *StatusName(u32 status)
    {
        if ((status & 0x07) != 0) return "asleep";
        if ((status & 0x08) != 0) return "poisoned";
        if ((status & 0x10) != 0) return "burned";
        if ((status & 0x20) != 0) return "frozen";
        if ((status & 0x40) != 0) return "paralysed";
        if ((status & 0x80) != 0) return "badly poisoned";
        return nullptr;
    }

    /// Percentage of HP remaining, rounded so that only a truly fainted
    /// Pokemon reads as 0 and only a truly full one reads as 100. A sliver of
    /// health rounding down to "0 percent" would be actively misleading.
    inline u32 HpPercent(u16 current, u16 max)
    {
        if (max == 0) return 0;
        if (current == 0) return 0;
        if (current >= max) return 100;
        const u32 pct = (static_cast<u32>(current) * 100u) / max;
        return pct == 0 ? 1 : pct;
    }

    /// The player's own HP: exact numbers, because they act on them.
    ///
    ///     "18 of 35"
    inline std::string HpExact(u16 current, u16 max)
    {
        return detail::Num(current) + " of " + detail::Num(max);
    }

    /// An opponent's HP: percentage only, matching what the game shows a
    /// sighted player. Giving exact values here would be an advantage, which
    /// rule 2 rules out.
    inline std::string HpApproximate(u16 current, u16 max)
    {
        if (current == 0) return "fainted";
        return detail::Num(HpPercent(current, max)) + " percent";
    }

    /// How a Pokemon is named aloud. A nickname replaces the species outright
    /// at terse verbosity -- the player chose that name and knows what it is.
    /// Above terse, the species follows so an unfamiliar nickname still
    /// identifies the creature.
    inline std::string Name(const PokemonInfo &p, Verbosity v)
    {
        const bool nicknamed = !detail::Empty(p.nickname);
        if (!nicknamed)
            return p.species;
        if (v == Verbosity::Terse)
            return p.nickname;
        return std::string(p.nickname) + ", " + p.species;
    }

    /// One party slot.
    ///
    ///   terse:   "Pikachu, level 50, 118 of 155"
    ///   normal:  "2 of 6. Pikachu, level 50, 118 of 155 hit points, burned."
    ///   verbose: adds ability and held item.
    inline std::string PartyMember(const PokemonInfo &p, u32 index, u32 total,
                                   Verbosity v = Verbosity::Normal)
    {
        std::string s;

        if (v != Verbosity::Terse && total > 0)
            s += detail::Num(index) + " of " + detail::Num(total) + ". ";

        s += Name(p, v);
        s += ", level " + detail::Num(p.level);

        if (p.hpCurrent == 0)
        {
            s += ", fainted";
        }
        else
        {
            s += ", " + HpExact(p.hpCurrent, p.hpMax);
            if (v != Verbosity::Terse)
                s += " hit points";
        }

        const char *status = StatusName(p.status);
        if (status != nullptr && p.hpCurrent != 0)
            s += ", " + std::string(status);

        if (v == Verbosity::Verbose)
        {
            if (!detail::Empty(p.ability))
                s += ", ability " + std::string(p.ability);
            if (!detail::Empty(p.heldItem))
                s += ", holding " + std::string(p.heldItem);
        }

        s += ".";
        return s;
    }

    /// A menu entry with its place in the list, so the player can tell where
    /// they are without counting keypresses.
    ///
    ///     "Potion, 3 of 12"
    inline std::string MenuItem(const char *name, u32 index, u32 count)
    {
        std::string s = detail::Empty(name) ? "blank" : name;
        if (count > 0)
            s += ", " + detail::Num(index) + " of " + detail::Num(count);
        return s;
    }

    /// A menu entry that also carries a quantity, as the bag does.
    ///
    ///     "Potion, 5, 3 of 12"
    inline std::string MenuItemWithQuantity(const char *name, u32 quantity,
                                            u32 index, u32 count)
    {
        std::string s = detail::Empty(name) ? "blank" : name;
        s += ", " + detail::Num(quantity);
        if (count > 0)
            s += ", " + detail::Num(index) + " of " + detail::Num(count);
        return s;
    }

    /// Compass direction from a facing value. Spoken in full: "north" is
    /// unambiguous where "N" is not.
    inline const char *Facing(u8 facing)
    {
        switch (facing & 3)
        {
            case 0:  return "north";
            case 1:  return "east";
            case 2:  return "south";
            default: return "west";
        }
    }

    /// Where the player is.
    ///
    ///   terse:  "Route 4"
    ///   normal: "Route 4, facing north"
    ///   verbose: adds coordinates.
    inline std::string Position(const char *mapName, s32 x, s32 y, u8 facing,
                                Verbosity v = Verbosity::Normal)
    {
        std::string s = detail::Empty(mapName) ? "unknown area" : mapName;

        if (v != Verbosity::Terse)
            s += ", facing " + std::string(Facing(facing));

        if (v == Verbosity::Verbose)
        {
            // Coordinates are for orientation and retracing steps, so they are
            // verbose-only -- speaking them by default is noise.
            s += ", at " + detail::Num(static_cast<u32>(x < 0 ? -x : x));
            if (x < 0) s += " west";
            s += ", " + detail::Num(static_cast<u32>(y < 0 ? -y : y));
            if (y < 0) s += " north";
        }

        s += ".";
        return s;
    }

    /// Both active Pokemon in a battle. The player's own HP is exact; the
    /// opponent's is a percentage, which is all the game shows.
    inline std::string BattleStatus(const PokemonInfo &mine, const PokemonInfo &theirs,
                                    Verbosity v = Verbosity::Normal)
    {
        std::string s = Name(mine, v) + ", level " + detail::Num(mine.level) + ", ";
        s += (mine.hpCurrent == 0) ? "fainted" : HpExact(mine.hpCurrent, mine.hpMax);

        const char *myStatus = StatusName(mine.status);
        if (myStatus != nullptr && mine.hpCurrent != 0)
            s += ", " + std::string(myStatus);

        s += ". Opponent " + Name(theirs, v);
        s += ", level " + detail::Num(theirs.level) + ", ";
        s += HpApproximate(theirs.hpCurrent, theirs.hpMax);

        const char *theirStatus = StatusName(theirs.status);
        if (theirStatus != nullptr && theirs.hpCurrent != 0)
            s += ", " + std::string(theirStatus);

        s += ".";
        return s;
    }

    /// A move on the battle menu.
    ///
    ///     "Thunderbolt, 15 of 15 PP"
    inline std::string MoveEntry(const char *move, u32 pp, u32 ppMax,
                                 u32 index, u32 count,
                                 Verbosity v = Verbosity::Normal)
    {
        std::string s = detail::Empty(move) ? "blank" : move;

        if (pp == 0)
            s += ", no PP left";
        else
            s += ", " + detail::Num(pp) + " of " + detail::Num(ppMax) + " PP";

        if (v != Verbosity::Terse && count > 0)
            s += ", " + detail::Num(index) + " of " + detail::Num(count);

        return s;
    }

    /// What is on the tile the player faces. Kept short: this is spoken
    /// constantly and every extra word costs the player time.
    inline std::string Facing(const char *what, Verbosity v = Verbosity::Normal)
    {
        if (detail::Empty(what))
            return (v == Verbosity::Terse) ? "clear" : "nothing ahead.";
        if (v == Verbosity::Terse)
            return what;
        return std::string(what) + " ahead.";
    }

}} // namespace xyoras::phrases

#endif
