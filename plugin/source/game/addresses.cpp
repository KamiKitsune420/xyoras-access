/*
 * XYORAS Access — the address table.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * THIS IS THE ONLY FILE ALLOWED TO CONTAIN ADDRESS LITERALS.
 * Everything else reaches them through game::Addr(). See rule 4 in CLAUDE.md.
 *
 * Every entry states the game series and the update version it was verified
 * against. An entry marked UNVERIFIED has not been confirmed by this project
 * and must not be trusted until it has been.
 *
 * Provenance: the UNVERIFIED entries below are transcribed from the community
 * Gen 6 CTRPluginFramework plugin lineage (GPL-3.0) -- see
 * "AI docks/12-research-log.md" and "AI docks/14-legal-and-licensing.md".
 * Method for confirming them: "AI docks/04-gen6-reverse-engineering.md".
 */
#include "xyoras/game.hpp"

namespace xyoras { namespace game { namespace addr {

    // -------------------------------------------------------------------------
    // Save-backed trainer block
    //
    // These cluster together in memory: name, ID, money, language, and play
    // time all sit within a few hundred bytes of each other. When hunting for
    // a new save-derived value, look here first.
    // -------------------------------------------------------------------------

    /// UNVERIFIED — trainer name, UTF-16.
    const AddrPair kTrainerName   = { 0x8C79C84, 0x8C81388 };

    /// UNVERIFIED — trainer ID block (TID/SID).
    const AddrPair kTrainerId     = { 0x8C79C43, 0x8C81347 };

    /// UNVERIFIED — money, u32.
    const AddrPair kMoney         = { 0x8C82B90, 0x8C8B35C };

    /// UNVERIFIED — Battle Points, u16.
    const AddrPair kBattlePoints  = { 0x8C82B94, 0x8C8B360 };

    /// UNVERIFIED — play time.
    const AddrPair kPlayTime      = { 0x8C79C69, 0x8C8136D };

    /// UNVERIFIED — in-game language. Read at startup: an English output layer
    /// over a non-English save produces nonsense, so we warn instead.
    const AddrPair kGameLanguage  = { 0x8C79CB8, 0x8C813BC };

    // -------------------------------------------------------------------------
    // Bag
    // -------------------------------------------------------------------------

    /// UNVERIFIED — general item pocket.
    const AddrPair kItemPocket    = { 0x8C6A6AC, 0x8C71DC0 };

    /// UNVERIFIED — medicine pocket.
    const AddrPair kMedicinePocket = { 0x8C6A6E0, 0x8C71DE8 };

    // -------------------------------------------------------------------------
    // Storage and Pokedex
    // -------------------------------------------------------------------------

    /// UNVERIFIED — box storage base.
    const AddrPair kBoxBase       = { 0x8C861C8, 0x8C9E134 };

    /// UNVERIFIED — Pokedex seen/caught block.
    const AddrPair kPokedexBlock  = { 0x8C6AC26, 0x8C7232A };

    // -------------------------------------------------------------------------
    // Battle
    // -------------------------------------------------------------------------

    /// UNVERIFIED — base of the battle Pokemon array.
    const AddrPair kBattleSlots   = { 0x81FF744, 0x81FEEC8 };

    /// UNVERIFIED — pointer used to detect whether a battle is in progress.
    const AddrPair kBattleState   = { 0x81FB170, 0x81FB478 };

    /// Distance between consecutive battle slots.
    ///
    /// Note this is NOT sizeof(PK6): the game keeps extra battle-only state
    /// after each entry. Same value on both series.
    const u32 kBattleSlotStride   = 0x1E4;

    /// Slots in the battle array (two sides, three each for a triple battle).
    const u32 kBattleSlotCount    = 6;

    // -------------------------------------------------------------------------
    // Not yet found — the work of Phase 2
    //
    // Highest value first. See "AI docks/04-gen6-reverse-engineering.md" for
    // what each unlocks and "AI docks/11-roadmap.md" for the tasks.
    //
    //   - Message-box render path   -> all dialogue and battle text at once
    //   - Player coordinates + map  -> every navigation feature
    //   - Menu cursor state         -> menu narration
    //   - Map collision/tile data   -> facing scan, surroundings scan
    //   - Party structure in RAM    -> party readout outside battle
    // -------------------------------------------------------------------------

}}} // namespace xyoras::game::addr
