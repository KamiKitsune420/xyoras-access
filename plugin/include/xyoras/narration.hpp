/*
 * XYORAS Access — turning the text on screen into the right utterances.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * A Gen 6 screen carries up to 155 `nw::lyt::TextBox` panes at once. Speaking
 * all of them would bury the player, so the question this file answers is
 * which of them to say.
 *
 * The rule is: **say what changed.** Static labels -- a menu heading, a button
 * caption -- are part of the furniture and the player learns them once. What
 * matters is the line that just appeared: the dialogue, the newly focused
 * item, the battle message.
 *
 * That makes the first sighting of a screen a special case. Arriving somewhere
 * new, every pane is "new", and reading all of them aloud would be exactly the
 * flood we are trying to avoid. So the first poll after a context change
 * records what is there without speaking it, and only later changes are
 * spoken. The player can still hear the whole screen on demand -- that is what
 * the read-screen hotkey is for.
 *
 * Header-only and 3DS-free, so the host tests exercise the shipped policy.
 */
#ifndef XYORAS_NARRATION_HPP
#define XYORAS_NARRATION_HPP

#include "xyoras/common.hpp"
#include "xyoras/screentext.hpp"
#include "xyoras/textbox.hpp"

#include <map>
#include <string>
#include <vector>

namespace xyoras { namespace narration {

    /// One pane's text as observed this poll.
    struct Observation
    {
        u32         id;      ///< the object's address, stable while it lives
        std::string text;

        Observation() : id(0) {}
        Observation(u32 i, const std::string &t) : id(i), text(t) {}
    };

    /// Watches every text pane on screen and reports what is worth saying.
    class Narrator
    {
    public:
        Narrator() : baselinePending_(true) {}

        /// Feed every visible pane, then take the utterances back.
        ///
        /// Panes absent from `observed` are treated as gone: their state is
        /// dropped, both to bound memory and so the same text reappearing
        /// later is spoken again.
        void Poll(const std::vector<Observation> &observed,
                  std::vector<std::string> &toSpeak)
        {
            toSpeak.clear();

            std::map<u32, screentext::Tracker> next;
            bool settledSomething = false;

            for (u32 i = 0; i < observed.size(); ++i)
            {
                const Observation &o = observed[i];

                // Ignore panes holding things that are not language --
                // separator rows, single glyphs, bare numbers.
                if (!textbox::WorthSpeaking(o.text))
                    continue;

                std::map<u32, screentext::Tracker>::iterator it = trackers_.find(o.id);
                screentext::Tracker t = (it != trackers_.end())
                                            ? it->second
                                            : screentext::Tracker();

                std::string settled;
                const bool fired = t.Update(o.text, settled);

                // On the first poll after a context change, record what is on
                // screen without speaking it. Otherwise arriving anywhere new
                // reads out the entire screen.
                if (fired)
                {
                    settledSomething = true;
                    if (!baselinePending_)
                        toSpeak.push_back(settled);
                }

                next[o.id] = t;
            }

            trackers_.swap(next);

            // The baseline lifts on the poll where text first SETTLES, not
            // merely on the first poll. Text needs several polls to settle, so
            // lifting earlier would leave the baseline already spent by the
            // time the opening screen finished settling -- and the whole
            // screen would be read out, which is the flood this exists to
            // prevent.
            if (baselinePending_ && settledSomething)
                baselinePending_ = false;
        }

        /// Everything currently on screen, for the read-screen hotkey.
        ///
        /// Deliberately separate from Poll: this is what the player asked for,
        /// so it ignores what has already been spoken and simply reports.
        void ReadAll(const std::vector<Observation> &observed,
                     std::vector<std::string> &out) const
        {
            out.clear();
            for (u32 i = 0; i < observed.size(); ++i)
                if (textbox::WorthSpeaking(observed[i].text))
                    out.push_back(observed[i].text);
        }

        /// Forget everything and take the next poll as a fresh baseline.
        ///
        /// For real context changes -- entering a battle, opening a menu --
        /// where the previous screen's text says nothing about this one.
        void NewContext()
        {
            trackers_.clear();
            baselinePending_ = true;
        }

        /// How many panes are being tracked. Exposed so a runaway can be
        /// noticed rather than silently eating memory.
        u32 TrackedCount() const { return static_cast<u32>(trackers_.size()); }

        bool BaselinePending() const { return baselinePending_; }

    private:
        std::map<u32, screentext::Tracker> trackers_;
        bool baselinePending_;
    };

}} // namespace xyoras::narration

#endif
