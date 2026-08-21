/*
 * XYORAS Access — the modifier-held control layer.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Gen 6 uses nearly every button, so the mod puts its own commands behind a
 * held modifier: ZL on a New 3DS, or L+R together on an Old 3DS. The scheme is
 * in "AI docks/02-accessibility-design.md".
 *
 * One command has no partner key: reading the whole screen. Every screen
 * reader has a "say all", it is the command a player reaches for most, and
 * there is no free button left to give it. So it is the modifier TAPPED ALONE
 * -- pressed and released without any other key. That works identically on
 * both console models and costs no button at all.
 *
 * The consequence is that the modifier cannot act until it is released, since
 * until then there is no way to know whether a partner key is coming. That is
 * only a few frames and applies to nothing else.
 *
 * The state machine lives here, free of anything 3DS-specific, so the host
 * tests drive the shipped code rather than a copy of it. hotkeys.cpp does
 * nothing but feed it what the buttons are doing.
 */
#ifndef XYORAS_HOTKEYS_HPP
#define XYORAS_HOTKEYS_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace hotkeys {

    /// Button bits, matching CTRPluginFramework::Key so hotkeys.cpp can pass
    /// the controller state straight through. Duplicated rather than included
    /// because this header has to build on a PC, where CTRPF does not exist.
    namespace key {
        constexpr u32 kA      = 1u << 0;
        constexpr u32 kB      = 1u << 1;
        constexpr u32 kSelect = 1u << 2;
        constexpr u32 kStart  = 1u << 3;
        constexpr u32 kR      = 1u << 8;
        constexpr u32 kL      = 1u << 9;
        constexpr u32 kX      = 1u << 10;
        constexpr u32 kY      = 1u << 11;
        constexpr u32 kZL     = 1u << 14;
        constexpr u32 kZR     = 1u << 15;
    }

    /// What the player asked for. Only commands that actually exist are
    /// listed; the rest of the control scheme is added as the features land.
    enum class Action
    {
        None = 0,
        ReadScreen,     ///< Modifier tapped alone: read everything on screen.
        RepeatLast,     ///< Modifier + A.
        StopSpeech      ///< Modifier + B.
    };

    /// The buttons that make up the modifier itself, on either model.
    constexpr u32 kModifierKeys = key::kZL | key::kL | key::kR;

    /// True while the modifier is held, on either console model.
    inline bool ModifierHeld(u32 keys)
    {
        if ((keys & key::kZL) != 0)
            return true;
        return (keys & key::kL) != 0 && (keys & key::kR) != 0;
    }

    /// Turns held-button state into commands.
    ///
    /// Fed the full set of currently-held buttons once a frame. Edge detection
    /// is done here rather than by the caller so the whole behaviour -- including
    /// what happens across a release -- is in one testable place.
    class ChordReader
    {
    public:
        ChordReader() : prevKeys_(0), modActive_(false), consumed_(false) {}

        Action Update(u32 keys)
        {
            const bool modNow = ModifierHeld(keys);

            // Only a key that was NOT held a frame ago counts, so holding
            // Modifier + A does not fire over and over.
            const u32 pressed = keys & ~prevKeys_;
            prevKeys_ = keys;

            if (modNow)
            {
                if (!modActive_)
                {
                    modActive_ = true;
                    consumed_  = false;
                }

                // The modifier's own buttons are not partner keys. This
                // matters for L+R: the second of the two arrives as a fresh
                // press on the very frame the chord starts, and counting it
                // would consume every chord before it began.
                const u32 partner = pressed & ~kModifierKeys;

                Action action = Action::None;
                if ((partner & key::kA) != 0)
                    action = Action::RepeatLast;
                else if ((partner & key::kB) != 0)
                    action = Action::StopSpeech;

                if (action != Action::None)
                {
                    // A partner key was used, so the release stays silent.
                    consumed_ = true;
                    return action;
                }

                // Any other button counts as using the chord too. Otherwise
                // Modifier + X -- a command that does not exist yet -- would
                // read the whole screen on release, which is not what the
                // player asked for.
                if (partner != 0)
                    consumed_ = true;

                return Action::None;
            }

            // The modifier is not held. If it has just come up, decide whether
            // the tap stands on its own.
            if (modActive_)
            {
                modActive_ = false;

                if (!consumed_)
                {
                    consumed_ = true;
                    return Action::ReadScreen;
                }
            }

            return Action::None;
        }

        /// Forget any chord in progress -- for when the settings menu opens
        /// and takes the buttons away mid-hold.
        void Reset()
        {
            prevKeys_  = 0;
            modActive_ = false;
            consumed_  = false;
        }

        bool InChord() const { return modActive_; }

    private:
        u32  prevKeys_;
        bool modActive_;
        bool consumed_;
    };

}} // namespace xyoras::hotkeys

namespace xyoras { namespace features { namespace hotkeys {

    /// Reads the controller and dispatches whatever the chord reader returns.
    /// Called once per frame from the plugin menu's frame callback, so it does
    /// as little as possible. Implemented in hotkeys.cpp.
    void Update(void);

    /// Drop any chord in progress -- for when the settings menu takes the
    /// buttons away mid-hold.
    void Reset(void);

}}} // namespace xyoras::features::hotkeys

#endif
