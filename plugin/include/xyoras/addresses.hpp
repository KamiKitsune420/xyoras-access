/*
 * XYORAS Access — declarations for the address table.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The values themselves live in `plugin/source/game/addresses.cpp`, which is
 * the only file allowed to contain address literals (rule 4 in CLAUDE.md).
 * This header exists so feature code can name an entry without knowing what
 * it holds -- it resolves through game::Addr(), which returns 0 for a series
 * we have no address for, and the memory helpers treat 0 as a failed read.
 *
 * Each entry's provenance, verification status and meaning are documented at
 * its definition, not here. Read addresses.cpp before trusting one.
 */
#ifndef XYORAS_ADDRESSES_HPP
#define XYORAS_ADDRESSES_HPP

#include "xyoras/game.hpp"

namespace xyoras { namespace game { namespace addr {

    // Save-backed trainer block -- all UNVERIFIED.
    extern const AddrPair kTrainerName;
    extern const AddrPair kTrainerId;
    extern const AddrPair kMoney;
    extern const AddrPair kBattlePoints;
    extern const AddrPair kPlayTime;
    extern const AddrPair kGameLanguage;

    // Bag -- UNVERIFIED.
    extern const AddrPair kItemPocket;
    extern const AddrPair kMedicinePocket;

    // Storage and Pokedex -- UNVERIFIED.
    extern const AddrPair kBoxBase;
    extern const AddrPair kPokedexBlock;

    // Battle -- UNVERIFIED.
    extern const AddrPair kBattleSlots;
    extern const AddrPair kBattleState;
    extern const u32      kBattleSlotStride;
    extern const u32      kBattleSlotCount;

    // Vtables. Verified against a running Pokemon X; the ORAS column is 0,
    // meaning Addr() will return 0 there and every dependent feature will
    // stay quiet rather than read a wrong address.
    extern const AddrPair kVtStrBuf;
    extern const AddrPair kVtMsgData;
    extern const AddrPair kVtStrWin;
    extern const AddrPair kVtTalkWindow;
    extern const AddrPair kVtTalkWindowGra;
    extern const AddrPair kVtMsgCursor;
    extern const AddrPair kVtMenuWindow;
    extern const AddrPair kVtMenuWindowSystem;
    extern const AddrPair kVtMsgWin;
    extern const AddrPair kVtTextBox;
    extern const AddrPair kVtPane;

    // Pane geometry, within an nw::lyt::Pane. Only kPaneTranslateY is
    // verified; read the definitions before trusting the others.
    extern const u32 kPaneTranslateX;
    extern const u32 kPaneTranslateY;
    extern const u32 kPaneTranslateZ;
    extern const u32 kPaneSizeW;
    extern const u32 kPaneSizeH;


}}} // namespace xyoras::game::addr

#endif
