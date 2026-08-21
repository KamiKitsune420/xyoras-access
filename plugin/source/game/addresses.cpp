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
#include "xyoras/addresses.hpp"

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
    // C++ vtables (XY only so far)
    //
    // Gen 6 ships with RTTI intact, so a class name can be followed back to
    // its vtable -- see tools/find_vtables.py. These classes are compiled into
    // code.bin, which loads at a constant base, so unlike anything in a CRO
    // their addresses do not move between boots.
    //
    // A vtable pointer is the first word of every instance, which makes these
    // usable two ways: scan the heap for one to find the live object, or hook
    // a virtual function to intercept the call.
    //
    // VERIFIED IN A RUNNING GAME (Pokemon X, under Azahar): reading these
    // addresses in the live process returns exactly the bytes static analysis
    // predicted, which confirms both the addresses and that code.bin is based
    // at 0x00100000. What is NOT yet confirmed is that scanning for one finds
    // a live object -- that needs a message box on screen.
    //
    // These are the values an object stores in its first word, i.e. the
    // address of the FIRST VIRTUAL FUNCTION. The Itanium ABI lays a vtable out
    // as [offset-to-top][typeinfo][fn0]... and the vptr points at fn0, not at
    // the start. Verified directly:
    //
    //     0x005970F8  00000000   offset-to-top
    //     0x005970FC  0057DB64   typeinfo
    //     0x00597100  00332610   fn0   <-- what a TalkWindow contains
    //
    // Scanning for the typeinfo slot instead would find nothing at all.
    //
    // The ORAS column is zero because no ORAS executable has been analysed.
    // -------------------------------------------------------------------------

    /// gfl::str::StrBuf -- a formatted string. The likeliest place to read
    /// dialogue text after the game applies its own substitutions.
    const AddrPair kVtStrBuf       = { 0x00598570, 0 };

    /// gfl::str::MsgData -- the message archive loader.
    const AddrPair kVtMsgData      = { 0x005985B4, 0 };

    /// gfl::str::StrWin
    const AddrPair kVtStrWin       = { 0x00598580, 0 };

    /// app::tool::TalkWindow -- the dialogue box itself.
    const AddrPair kVtTalkWindow   = { 0x00597100, 0 };

    /// app::tool::TalkWindowGra
    const AddrPair kVtTalkWindowGra = { 0x00597288, 0 };

    /// app::tool::MsgCursor -- the advance-text cursor.
    const AddrPair kVtMsgCursor    = { 0x005974C8, 0 };

    /// app::tool::MenuWindow
    const AddrPair kVtMenuWindow   = { 0x005970E0, 0 };

    /// app::tool::MenuWindowSystem
    const AddrPair kVtMenuWindowSystem = { 0x00597338, 0 };

    /// print::MsgWin
    const AddrPair kVtMsgWin       = { 0x00599A60, 0 };

    /// nw::lyt::TextBox -- the NintendoWare layout text pane. THIS is where
    /// on-screen text actually lives, not gfl::str::StrBuf (which turns out to
    /// be a pool of mostly-empty formatting buffers).
    ///
    /// Live instance counts track the screen exactly: 0 during a transition,
    /// 155 on a text-heavy setup screen, 21 on a sparse one.
    const AddrPair kVtTextBox      = { 0x00596148, 0 };

    /// nw::lyt::Pane -- layout panes generally.
    const AddrPair kVtPane         = { 0x00595F24, 0 };

    // -------------------------------------------------------------------------
    // Pane geometry, within an nw::lyt::Pane
    //
    // Found by dumping the 0xD4 bytes before the string pointer for every pane
    // on the language-selection screen of Pokemon X, and looking for the field
    // that moves down a list whose order is visible on screen. See
    // "AI docks/12-research-log.md".
    //
    // Words 0x80 through 0xAF form a 3x4 row-major matrix. Every pane observed
    // held an identity rotation, which makes the translation column easy to
    // read off:
    //
    //   +0x80  1.0   0.0   0.0   [+0x8C  tx]
    //   +0x90  0.0   1.0   0.0   [+0x9C  ty]
    //   +0xA0  0.0   0.0   1.0   [+0xAC  tz]
    // -------------------------------------------------------------------------

    /// Vertical position of a pane.
    ///
    /// VERIFIED on Pokemon X version 0. Down the language list it read
    /// 15, 0, -10, -40, -70, -100, -130, -160, -190 -- matching the on-screen
    /// order exactly, at an even 30-unit spacing for the seven language rows.
    /// Larger is higher on the screen.
    const u32 kPaneTranslateY = 0x9C;

    /// Horizontal position. UNVERIFIED: every pane on the only screen examined
    /// sat at tx = 0, so its position in the matrix is inferred from ty, not
    /// confirmed. A screen with items side by side would settle it.
    const u32 kPaneTranslateX = 0x8C;

    /// Depth. UNVERIFIED, and there is something to explain here: visible panes
    /// read -1.0, while three panes that appeared to be duplicates or offscreen
    /// read 0.0. That may be how a pane not in the visible tree is marked, but
    /// one screen is not evidence, and it has not been checked.
    const u32 kPaneTranslateZ = 0xAC;

    /// Width and height, as two floats. UNVERIFIED but strongly suggested:
    /// the instruction pane read 290 x 200 and every single-line label read
    /// 290 x 20, which are plausible sizes in 3DS layout units.
    const u32 kPaneSizeW = 0x48;
    const u32 kPaneSizeH = 0x4C;

    // The offset of the string pointer within a TextBox (+0xD4) is NOT here.
    // It is a structure offset rather than an address, it is the same for
    // every series, and the code that uses it -- textbox.hpp -- must stay free
    // of anything 3DS-specific so the host tests can drive it. It is defined
    // once, as textbox::kStringOffset, with the evidence for it. Defining it
    // twice is how the two copies drift apart.

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
