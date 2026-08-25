/*
 * XYORAS Access — deciding when on-screen text is worth speaking.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Reading text is solved (see textbox.hpp). Knowing *when* to say it is the
 * harder half, and it is what separates a mod that narrates from one that
 * chatters.
 *
 * The problem that shapes this file: Gen 6 message boxes type text out one
 * character at a time. Polling a message box mid-animation sees "Y", then
 * "Yo", then "You", then "You g"... Speaking every change would stutter the
 * first syllable of every line dozens of times and be completely unusable.
 *
 * So a string is only spoken once it has stopped changing. While the animation
 * runs the text keeps differing from the last poll and the counter resets;
 * when it settles, the finished line is spoken once.
 *
 * Header-only and 3DS-free, so the host tests exercise the shipped logic.
 */
#ifndef XYORAS_SCREENTEXT_HPP
#define XYORAS_SCREENTEXT_HPP

#include "xyoras/common.hpp"

#include <string>

namespace xyoras { namespace screentext {

    /// Polls a string must be unchanged for before it counts as finished.
    ///
    /// Too low and a slow typing animation gets spoken in fragments; too high
    /// and the player waits after the text has plainly stopped. At a ~60 Hz
    /// poll this is about 50 ms.
    constexpr u32 kSettlePolls = 3;

    /// Watches one source of text and reports it once it has settled.
    ///
    /// One tracker per thing being watched -- the dialogue box, the menu
    /// cursor, and so on -- so that a busy menu cannot suppress a story line.
    class Tracker
    {
    public:
        Tracker() : stable_(0), hasSpoken_(false) {}

        /// Feed the current text every poll. Returns true, and fills `out`,
        /// exactly once per settled string.
        ///
        /// Pass an empty string when the text has gone from the screen; that
        /// clears the memory of what was said, so the same line spoken by the
        /// same sign a second time is read again rather than silently skipped.
        bool Update(const std::string &current, std::string &out)
        {
            if (current.empty())
            {
                // Text left the screen. Forget it, so a repeat visit speaks.
                pending_.clear();
                spoken_.clear();
                hasSpoken_ = false;
                stable_ = 0;
                return false;
            }

            if (current != pending_)
            {
                // Still changing -- almost certainly mid-animation.
                pending_ = current;
                stable_ = 0;
                return false;
            }

            // Unchanged since the last poll.
            if (stable_ < kSettlePolls)
            {
                ++stable_;
                if (stable_ < kSettlePolls)
                    return false;
            }
            else
            {
                return false;   // already reported this one
            }

            if (hasSpoken_ && pending_ == spoken_)
                return false;   // settled on what we already said

            spoken_ = pending_;
            hasSpoken_ = true;
            out = spoken_;
            return true;
        }

        /// True while this pane holds text that has not finished settling --
        /// i.e. it is still mid-animation. Distinct from "nothing fired this
        /// poll", which is also true of a pane that is simply idle.
        bool Unsettled() const { return !pending_.empty() && stable_ < kSettlePolls; }

        /// What was last reported. Empty if nothing has been.
        const std::string &LastSpoken() const { return spoken_; }

        /// Forget everything. Used when the game changes context entirely --
        /// entering a battle, closing a menu -- so old text cannot suppress
        /// identical new text.
        void Reset()
        {
            pending_.clear();
            spoken_.clear();
            hasSpoken_ = false;
            stable_ = 0;
        }

    private:
        std::string pending_;    ///< what the last poll saw
        std::string spoken_;     ///< what was last reported
        u32         stable_;     ///< polls `pending_` has been unchanged
        bool        hasSpoken_;
    };

    /// True when `longer` looks like `shorter` with more typed on the end.
    ///
    /// Not used to decide when to speak -- the settle rule handles that on its
    /// own -- but useful for telling a typing animation apart from a genuinely
    /// new line, which is worth knowing when deciding whether to interrupt.
    inline bool IsContinuationOf(const std::string &shorter, const std::string &longer)
    {
        if (shorter.empty() || longer.size() <= shorter.size())
            return false;
        return longer.compare(0, shorter.size(), shorter) == 0;
    }

}} // namespace xyoras::screentext

#endif
