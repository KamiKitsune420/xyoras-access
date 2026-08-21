/*
 * XYORAS Access — title and version identification.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 */
#include "xyoras/game.hpp"

namespace xyoras { namespace game {

using CTRPluginFramework::Process;

namespace {

    Title  g_title   = Title::Unknown;
    Series g_series  = Series::Unknown;
    u16    g_version = 0;
    bool   g_identified = false;

    // Verified update versions, per capability. Each list is what THIS project
    // has actually confirmed against a running game -- not what someone else
    // reported, and not what seems likely.

    /// nw::lyt::TextBox discovery and its +0xD4 string offset.
    ///
    /// Version 0 is the base cartridge with no update installed. Confirmed by
    /// reading real displayed text out of a running Pokemon X -- "Your name?",
    /// "SAVE", "OPTIONS" and message-box content -- with the live instance
    /// count tracking the screen exactly. See "AI docks/12-research-log.md".
    ///
    /// That confirmation was made under emulation. It holds anyway: these
    /// addresses are in code.bin, which is the same file and the same fixed
    /// base on hardware. What emulation cannot vouch for is timing and audio,
    /// neither of which this capability depends on.
    ///
    /// An update changes code.bin, so a newer version is NOT covered.
    const u16 kLayoutTextXY[]   = { 0 };
    const u16 kLayoutTextORAS[] = { };

    /// Trainer block, bag, boxes, Pokedex. Inherited from the community Gen 6
    /// plugin lineage and never confirmed by this project on any version.
    const u16 kSaveDataXY[]   = { };
    const u16 kSaveDataORAS[] = { };

    /// Battle slots and state. Same provenance, same status.
    const u16 kBattleStateXY[]   = { };
    const u16 kBattleStateORAS[] = { };

    bool Contains(const u16 *list, u32 count, u16 value)
    {
        for (u32 i = 0; i < count; ++i)
            if (list[i] == value)
                return true;
        return false;
    }
}

void Identify(void)
{
    if (g_identified)
        return;

    const u64 titleId = Process::GetTitleID();

    switch (titleId)
    {
        case kTitleIdX:  g_title = Title::X;             g_series = Series::XY;   break;
        case kTitleIdY:  g_title = Title::Y;             g_series = Series::XY;   break;
        case kTitleIdOR: g_title = Title::OmegaRuby;     g_series = Series::ORAS; break;
        case kTitleIdAS: g_title = Title::AlphaSapphire; g_series = Series::ORAS; break;
        default:         g_title = Title::Unknown;       g_series = Series::Unknown; break;
    }

    g_version    = Process::GetVersion();
    g_identified = true;
}

Title  CurrentTitle(void)   { return g_title;   }
Series CurrentSeries(void)  { return g_series;  }
u16    CurrentVersion(void) { return g_version; }

const char *TitleName(void)
{
    switch (g_title)
    {
        case Title::X:             return "Pokemon X";
        case Title::Y:             return "Pokemon Y";
        case Title::OmegaRuby:     return "Pokemon Omega Ruby";
        case Title::AlphaSapphire: return "Pokemon Alpha Sapphire";
        default:                   return "an unrecognised game";
    }
}

bool IsVerified(Capability capability)
{
    const u16 *list  = nullptr;
    u32        count = 0;

    switch (capability)
    {
        case Capability::LayoutText:
            if (g_series == Series::XY)
            {
                list  = kLayoutTextXY;
                count = sizeof(kLayoutTextXY) / sizeof(u16);
            }
            else if (g_series == Series::ORAS)
            {
                list  = kLayoutTextORAS;
                count = sizeof(kLayoutTextORAS) / sizeof(u16);
            }
            break;

        case Capability::SaveData:
            if (g_series == Series::XY)
            {
                list  = kSaveDataXY;
                count = sizeof(kSaveDataXY) / sizeof(u16);
            }
            else if (g_series == Series::ORAS)
            {
                list  = kSaveDataORAS;
                count = sizeof(kSaveDataORAS) / sizeof(u16);
            }
            break;

        case Capability::BattleState:
            if (g_series == Series::XY)
            {
                list  = kBattleStateXY;
                count = sizeof(kBattleStateXY) / sizeof(u16);
            }
            else if (g_series == Series::ORAS)
            {
                list  = kBattleStateORAS;
                count = sizeof(kBattleStateORAS) / sizeof(u16);
            }
            break;
    }

    if (list == nullptr || count == 0)
        return false;   // unknown series, or nothing verified for it

    return Contains(list, count, g_version);
}

u32 Addr(const AddrPair &pair)
{
    switch (g_series)
    {
        case Series::XY:   return pair.xy;
        case Series::ORAS: return pair.oras;
        default:           return 0;   // treated as a failed read downstream
    }
}

}} // namespace xyoras::game
