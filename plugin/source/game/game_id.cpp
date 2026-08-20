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

    /// Update versions our address table has been checked against.
    ///
    /// Empty until Phase 2 re-verifies the inherited community offsets on real
    /// hardware (see "AI docks/11-roadmap.md" task 2.4). Until then every
    /// version reads as unsupported, which is the honest answer.
    const u16 kVerifiedXY[]   = { };
    const u16 kVerifiedORAS[] = { };

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

bool IsVersionSupported(void)
{
    switch (g_series)
    {
        case Series::XY:
            return Contains(kVerifiedXY, sizeof(kVerifiedXY) / sizeof(u16), g_version);
        case Series::ORAS:
            return Contains(kVerifiedORAS, sizeof(kVerifiedORAS) / sizeof(u16), g_version);
        default:
            return false;
    }
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
