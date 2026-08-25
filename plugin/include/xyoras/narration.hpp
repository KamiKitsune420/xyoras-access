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

    /// Adds a string unless it is already there.
    ///
    /// Real screens carry the same words on more than one pane -- the language
    /// screen in Pokemon X holds "Play Pokemon X in" twice, once per display
    /// line. Saying it twice tells the player nothing and costs them time.
    inline void AppendUnique(std::vector<std::string> &out, const std::string &text)
    {
        for (u32 i = 0; i < out.size(); ++i)
            if (out[i] == text)
                return;
        out.push_back(text);
    }

    /// Watches every text pane on screen and reports what is worth saying.
    class Narrator
    {
    public:
        Narrator() : baselinePending_(true), baselineSawSettle_(false), baselinePolls_(0) {}

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
            bool anyUnsettled = false;

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
                        AppendUnique(toSpeak, settled);
                }

                if (t.Unsettled())
                    anyUnsettled = true;

                next[o.id] = t;
            }

            trackers_.swap(next);

            // A burst of lines settling together is a menu or panel opening,
            // not the player causing something. Reading every option aloud is
            // the flood this design exists to avoid, and it is what made the
            // repeat-last key replay an entire menu.
            //
            // Audio-game menus solve this by speaking an intro that carries the
            // count, then only the focused item (see nvgt's menu.nvgt), and
            // NVDA does the same thing by speaking the focused object rather
            // than the screen. Announcing the count is the half that can be
            // done without knowing which item is focused; the other half needs
            // the selection highlight, which is a Picture rather than a
            // TextBox and is not yet identified.
            if (toSpeak.size() > kBurstIsAMenu)
            {
                const std::size_t count = toSpeak.size();
                toSpeak.clear();
                toSpeak.push_back(CountPhrase(count));
            }

            // The baseline has to cover the WHOLE arriving screen, not just
            // the first poll in which something settles.
            //
            // Panes settle at different times: a short button caption settles
            // while a typed-out message is still animating. Lifting on the
            // first settle therefore leaves the rest of the same screen to be
            // spoken as though it were new -- which is heard as two unrelated
            // lines talking over each other on arrival.
            //
            // So keep absorbing while settles are still arriving, and lift on
            // the first quiet poll after them. The poll cap is the escape
            // hatch: a screen with a permanently animating pane would never go
            // quiet, and never speaking again is worse than one extra line.
            if (baselinePending_)
            {
                ++baselinePolls_;

                if (settledSomething)
                    baselineSawSettle_ = true;

                // Quiet means nothing fired AND nothing is still animating.
                // Without the second half, a pane part-way through typing looks
                // quiet simply because it has not fired yet, the baseline lifts
                // early, and that pane is spoken when it finally settles --
                // which is the very bug this is meant to prevent.
                if (baselineSawSettle_ && !settledSomething && !anyUnsettled)
                    baselinePending_ = false;

                if (baselinePolls_ >= kBaselineMaxPolls)
                    baselinePending_ = false;
            }
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
                    AppendUnique(out, observed[i].text);
        }

        /// Forget everything and take the next poll as a fresh baseline.
        ///
        /// For real context changes -- entering a battle, opening a menu --
        /// where the previous screen's text says nothing about this one.
        /// Polls the baseline may absorb before it lifts regardless. Long
        /// enough for a screen to finish arriving, short enough that a screen
        /// which never goes quiet still becomes audible.
        static const u32 kBaselineMaxPolls = 40;

        /// More lines than this settling in one poll means a screen arrived,
        /// not that something happened. Two is deliberate: a prompt plus its
        /// answer is normal, a five-option menu is not.
        static const std::size_t kBurstIsAMenu = 2;

        void NewContext()
        {
            trackers_.clear();
            baselinePending_ = true;
            baselineSawSettle_ = false;
            baselinePolls_ = 0;
        }

        /// How many panes are being tracked. Exposed so a runaway can be
        /// noticed rather than silently eating memory.
        u32 TrackedCount() const { return static_cast<u32>(trackers_.size()); }

        bool BaselinePending() const { return baselinePending_; }

        /// Decimal digits, without pulling stdio into a header the host tests
        /// compile as plain C++.
        static std::string Count(std::size_t n)
        {
            std::string digits;
            if (n == 0)
                return "0";
            while (n > 0)
            {
                digits.insert(digits.begin(), static_cast<char>('0' + (n % 10)));
                n /= 10;
            }
            return digits;
        }

        static std::string CountPhrase(std::size_t n)
        {
            std::string digits;
            if (n == 0)
                digits = "0";
            while (n > 0)
            {
                digits.insert(digits.begin(), static_cast<char>('0' + (n % 10)));
                n /= 10;
            }
            return digits + " items";
        }

        std::map<u32, screentext::Tracker> trackers_;
        bool baselinePending_;
        bool baselineSawSettle_;   ///< a settle has landed during this baseline
        u32  baselinePolls_;       ///< polls since the baseline began
    };

}} // namespace xyoras::narration

#endif
