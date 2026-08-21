/*
 * XYORAS Access — reading the buttons.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Runs from the plugin menu's frame callback, so it is on the game's thread
 * and must stay trivial: read the controller, hand the bits to the state
 * machine in hotkeys.hpp, dispatch whatever comes back. Every action it
 * dispatches either enqueues an utterance or sets a flag, and both return at
 * once (rule 3 in CLAUDE.md).
 *
 * The decision-making is deliberately not here. It lives in the header, which
 * has host tests, because "the mod said something I did not ask for" is the
 * kind of bug that is nearly impossible to diagnose on a console.
 */
#include "xyoras/common.hpp"
#include "xyoras/hotkeys.hpp"
#include "xyoras/narrate.hpp"
#include "xyoras/speech.hpp"

namespace xyoras { namespace features { namespace hotkeys {

namespace {

    /// Buttons the chord layer looks at.
    ///
    /// The D-pad, circle pad and C-stick are deliberately absent. A player is
    /// very often holding a direction -- walking is most of the game -- and to
    /// the state machine a direction arriving mid-chord is indistinguishable
    /// from a partner key, which would consume the chord and swallow the
    /// read-screen tap. They join this mask when the directional commands in
    /// "AI docks/02-accessibility-design.md" are actually implemented, and the
    /// state machine will need to tell walking from a deliberate press then.
    constexpr u32 kWatchedKeys =
        CTRPluginFramework::Key::A      | CTRPluginFramework::Key::B      |
        CTRPluginFramework::Key::X      | CTRPluginFramework::Key::Y      |
        CTRPluginFramework::Key::L      | CTRPluginFramework::Key::R      |
        CTRPluginFramework::Key::ZL     | CTRPluginFramework::Key::ZR     |
        CTRPluginFramework::Key::Start  | CTRPluginFramework::Key::Select;

    xyoras::hotkeys::ChordReader g_reader;
}

/// Called once per frame while the game is running.
void Update(void)
{
    using xyoras::hotkeys::Action;

    // GetKeysDown(false) is what is currently held, without the just-pressed
    // set folded in. The state machine does its own edge detection, and
    // feeding it both would make a single press look like two.
    const u32 keys = CTRPluginFramework::Controller::GetKeysDown(false) & kWatchedKeys;

    switch (g_reader.Update(keys))
    {
        case Action::ReadScreen:
            narrate::RequestReadScreen();
            break;

        case Action::RepeatLast:
            speech::RepeatLast();
            break;

        case Action::StopSpeech:
            speech::StopAll();
            break;

        case Action::DumpLayout:
            // No-ops unless the trace marker is on the card.
            narrate::RequestLayoutDump();
            break;

        case Action::None:
        default:
            break;
    }
}

/// The settings menu takes the buttons while it is open. Dropping the chord
/// in progress stops the modifier looking "released" on the way back out and
/// reading the screen unbidden.
void Reset(void)
{
    g_reader.Reset();
}

}}} // namespace xyoras::features::hotkeys
