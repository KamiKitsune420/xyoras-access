/*
 * XYORAS Access — game identification and guarded memory access.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * This layer READS the game. It never speaks. See "AI docks/09-coding-standards.md".
 */
#ifndef XYORAS_GAME_HPP
#define XYORAS_GAME_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace game {

    // -------------------------------------------------------------------------
    // Identification
    // -------------------------------------------------------------------------

    enum class Title  { Unknown, X, Y, OmegaRuby, AlphaSapphire };

    /// X and Y share a build; Omega Ruby and Alpha Sapphire share a different
    /// one. Addresses are per-series, not per-title.
    enum class Series { Unknown, XY, ORAS };

    constexpr u64 kTitleIdX  = 0x0004000000055D00ULL;
    constexpr u64 kTitleIdY  = 0x0004000000055E00ULL;
    constexpr u64 kTitleIdOR = 0x000400000011C400ULL;
    constexpr u64 kTitleIdAS = 0x000400000011C500ULL;

    /// Reads the running process's title ID and version. Call once at startup.
    void   Identify(void);

    Title  CurrentTitle(void);
    Series CurrentSeries(void);
    u16    CurrentVersion(void);

    /// Human-readable, for the settings menu and the startup banner.
    const char *TitleName(void);

    /// What has actually been checked against the running build.
    ///
    /// Verification is not one bit. The layout-text addresses were confirmed by
    /// reading real text out of a running game; the save and battle offsets are
    /// inherited from the community and have never been confirmed by this
    /// project. Gating both on a single flag would mean either keeping a
    /// working feature switched off, or switching on offsets nobody has
    /// checked. Neither is acceptable, so each is asked about separately.
    enum class Capability
    {
        /// nw::lyt::TextBox discovery and its string offset -- everything the
        /// narration feature reads.
        LayoutText,

        /// The trainer block, bag, boxes and Pokedex offsets in addresses.cpp.
        SaveData,

        /// Battle slots and battle state.
        BattleState
    };

    /// False when this capability has not been verified against the running
    /// game and update version. A feature that reads memory must stay off in
    /// that case — a confidently wrong reading is worse than an honest refusal.
    bool   IsVerified(Capability capability);

    // -------------------------------------------------------------------------
    // Addresses
    // -------------------------------------------------------------------------

    /// One address per series. Every literal in the project lives in
    /// addresses.cpp and is reached through Addr().
    struct AddrPair
    {
        u32 xy;
        u32 oras;
    };

    /// Resolves an AddrPair against the detected series. Returns 0 if the
    /// series is unknown, which the memory helpers treat as a failed read.
    u32 Addr(const AddrPair &pair);

    // -------------------------------------------------------------------------
    // Guarded memory access
    // -------------------------------------------------------------------------

    /// Gen 6 heap addresses observed in the wild fall inside this window.
    /// Anything outside it is a bad pointer, not data.
    constexpr u32 kHeapMin = 0x08000000;
    constexpr u32 kHeapMax = 0x08DF0000;

    bool InHeap(u32 address);

    /// All of these return false rather than faulting. A plugin fault takes
    /// the game down with it, and a blind player cannot read a crash screen.
    bool Read8  (u32 address, u8  &out);
    bool Read16 (u32 address, u16 &out);
    bool Read32 (u32 address, u32 &out);
    bool Read64 (u32 address, u64 &out);
    bool ReadBuf(u32 address, void *out, u32 size);

    /// Reads a pointer and range-checks the result before handing it back.
    bool ReadPtr(u32 address, u32 &out);

    /// Walks a chain of offsets, range-checking at every step.
    /// ReadChain(base, {0x10, 0x24}, out) == *(*(base) + 0x10) + 0x24
    bool ReadChain(u32 base, const u32 *offsets, u32 count, u32 &out);

    /// UTF-16LE, as the games store names. Stops at the first null.
    bool ReadUtf16(u32 address, u32 maxChars, std::string &out);

}} // namespace xyoras::game

#endif
