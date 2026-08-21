/*
 * XYORAS Access — the narration feature.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Watches the text the game draws and speaks what changes. See narrate.cpp
 * for how the pieces fit, and "AI docks/02-accessibility-design.md" for what
 * the player is meant to hear.
 *
 * Everything here returns immediately. The work happens on the feature's own
 * thread, because reading the game's heap is far too slow to do from a frame
 * callback (rule 3 in CLAUDE.md).
 */
#ifndef XYORAS_NARRATE_HPP
#define XYORAS_NARRATE_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace features { namespace narrate {

    /// Starts the polling thread. False when the running game is one whose
    /// addresses we have not verified, in which case the feature stays off
    /// rather than reading something wrong.
    bool Start(void);
    void Stop(void);

    /// Ask for everything on screen to be read aloud. Returns at once; the
    /// reading happens on the polling thread.
    void RequestReadScreen(void);

    /// Tell the narrator the screen has been replaced wholesale -- a battle
    /// starting, a menu opening -- so it takes a fresh baseline instead of
    /// announcing the new screen as a change from the old one.
    void RequestNewContext(void);

    /// Write a snapshot of every layout object on screen to the trace file.
    ///
    /// Diagnostic. Does nothing without /xyoras-access/trace-narration. Sit on
    /// a menu, move the cursor, ask for a snapshot, move again, ask again --
    /// whatever differs between the snapshots is what marks the selection.
    void RequestLayoutDump(void);

    /// How many text panes are currently being polled. For the diagnostics
    /// menu: a plausible number is evidence the scan is finding real objects.
    u32  TrackedPanes(void);

    /// Whether the polling thread is running.
    bool IsRunning(void);

}}} // namespace xyoras::features::narrate

#endif
