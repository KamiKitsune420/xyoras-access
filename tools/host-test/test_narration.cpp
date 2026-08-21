/*
 * XYORAS Access — host tests for choosing what to narrate.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * A real Gen 6 screen carries up to 155 text panes. The whole job here is
 * saying the one line that matters and staying quiet about the other 154, so
 * these tests are written around that: a screen full of furniture with one
 * thing changing in the middle of it.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/narration.hpp"

#include <string>
#include <vector>

using namespace xyoras;
using narration::Observation;

namespace {

    /// Settle a set of observations so anything new gets reported.
    void Settle(narration::Narrator &n, const std::vector<Observation> &obs,
                std::vector<std::string> &spoken)
    {
        spoken.clear();
        for (u32 i = 0; i < screentext::kSettlePolls + 1; ++i)
        {
            std::vector<std::string> out;
            n.Poll(obs, out);
            for (u32 j = 0; j < out.size(); ++j)
                spoken.push_back(out[j]);
        }
    }

    /// A menu's worth of static furniture, as a real screen would carry.
    std::vector<Observation> Furniture(void)
    {
        std::vector<Observation> obs;
        obs.push_back(Observation(0x08100000, "POKEMON"));
        obs.push_back(Observation(0x08100100, "BAG"));
        obs.push_back(Observation(0x08100200, "TRAINER CARD"));
        obs.push_back(Observation(0x08100300, "SAVE"));
        obs.push_back(Observation(0x08100400, "OPTIONS"));
        return obs;
    }

    void TestBaselineIsSilent(void)
    {
        test::Section("arriving at a screen is silent");

        // Reading out every label on arrival is the flood this whole design
        // exists to avoid.
        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);

        test::Equal(spoken.size(), 0u, "five labels on arrival say nothing");
        test::Check(!n.BaselinePending(), "but the baseline is now taken");
        test::Equal(n.TrackedCount(), 5u, "and all five are tracked");
    }

    void TestChangeIsSpoken(void)
    {
        test::Section("a line that appears is spoken");

        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);          // baseline

        // The dialogue box appears among the unchanged furniture.
        std::vector<Observation> withDialogue = Furniture();
        withDialogue.push_back(Observation(0x08200000, "You got a message!"));

        Settle(n, withDialogue, spoken);
        test::Equal(spoken.size(), 1u, "only the new line is spoken");
        if (spoken.size() == 1)
            test::EqualStr(spoken[0], "You got a message!", "and it is the right one");
    }

    void TestStaticFurnitureStaysSilent(void)
    {
        test::Section("unchanged furniture stays silent");

        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);

        // Many polls later, still nothing to say.
        for (u32 i = 0; i < 50; ++i)
        {
            std::vector<std::string> out;
            n.Poll(Furniture(), out);
            test::Equal(out.size(), 0u, i == 0 ? "and stays silent" : "");
            if (i > 0)
                break;   // one assertion is enough; the loop proves the rest
        }

        std::vector<std::string> out;
        for (u32 i = 0; i < 50; ++i)
            n.Poll(Furniture(), out);
        test::Equal(out.size(), 0u, "50 further polls say nothing");
    }

    void TestChangedLabelIsSpoken(void)
    {
        test::Section("a label whose text changes");

        // The focused menu entry changing is exactly the case that must speak.
        narration::Narrator n;
        std::vector<std::string> spoken;

        std::vector<Observation> obs;
        obs.push_back(Observation(0x08100000, "Potion"));
        Settle(n, obs, spoken);

        obs[0].text = "Super Potion";
        Settle(n, obs, spoken);

        test::Equal(spoken.size(), 1u, "the changed entry is spoken");
        if (spoken.size() == 1)
            test::EqualStr(spoken[0], "Super Potion", "with its new text");
    }

    void TestJunkIsFiltered(void)
    {
        test::Section("panes that are not language");

        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);

        // All of these came out of the real game alongside the real text.
        std::vector<Observation> obs = Furniture();
        obs.push_back(Observation(0x08300000, "-------------------"));
        obs.push_back(Observation(0x08300100, "12345"));
        obs.push_back(Observation(0x08300200, "A"));
        obs.push_back(Observation(0x08300300, "Real dialogue here"));

        Settle(n, obs, spoken);
        test::Equal(spoken.size(), 1u, "only the sentence survives the filter");
        if (spoken.size() == 1)
            test::EqualStr(spoken[0], "Real dialogue here", "the dashes and digits are dropped");
    }

    void TestDisappearedPanesAreForgotten(void)
    {
        test::Section("panes that go away");

        // Memory must not grow with every screen the player visits.
        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);
        test::Equal(n.TrackedCount(), 5u, "five tracked");

        std::vector<Observation> fewer;
        fewer.push_back(Observation(0x08100000, "POKEMON"));
        Settle(n, fewer, spoken);
        test::Equal(n.TrackedCount(), 1u, "the four that vanished are forgotten");
    }

    void TestReappearingTextSpeaksAgain(void)
    {
        test::Section("the same line from the same sign twice");

        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);

        std::vector<Observation> withSign = Furniture();
        withSign.push_back(Observation(0x08200000, "A wooden signpost."));
        Settle(n, withSign, spoken);
        test::Equal(spoken.size(), 1u, "spoken the first time");

        Settle(n, Furniture(), spoken);          // sign closes
        Settle(n, withSign, spoken);             // read it again
        test::Equal(spoken.size(), 1u, "and spoken again on a second reading");
    }

    void TestNewContextResets(void)
    {
        test::Section("a real context change");

        // Entering a battle must not be silenced by identical earlier text.
        narration::Narrator n;
        std::vector<std::string> spoken;

        std::vector<Observation> obs;
        obs.push_back(Observation(0x08100000, "Go! Pikachu!"));
        Settle(n, obs, spoken);
        test::Equal(spoken.size(), 0u, "first screen is the baseline");

        n.NewContext();
        test::Check(n.BaselinePending(), "the next poll is a fresh baseline");
        test::Equal(n.TrackedCount(), 0u, "and nothing is remembered");
    }

    void TestReadAllIgnoresHistory(void)
    {
        test::Section("read-screen on demand");

        // The player asked, so it reports regardless of what has been said.
        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);
        test::Equal(spoken.size(), 0u, "nothing spoken automatically");

        std::vector<std::string> all;
        n.ReadAll(Furniture(), all);
        test::Equal(all.size(), 5u, "but read-screen reports all five");
        if (all.size() == 5)
            test::EqualStr(all[0], "POKEMON", "in screen order");

        std::vector<Observation> withJunk = Furniture();
        withJunk.push_back(Observation(0x08300000, "-----"));
        n.ReadAll(withJunk, all);
        test::Equal(all.size(), 5u, "and still filters the junk");
    }

    void TestEmptyPollDoesNotWasteBaseline(void)
    {
        test::Section("a poll during a transition");

        // Mid-transition the screen can be empty. Spending the baseline there
        // would mean the next real screen gets read out in full.
        narration::Narrator n;
        std::vector<std::string> out;
        n.Poll(std::vector<Observation>(), out);
        test::Check(n.BaselinePending(), "an empty poll does not spend the baseline");

        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);
        test::Equal(spoken.size(), 0u, "so the first real screen is still silent");
    }
    void TestDuplicatePanesSpeakOnce(void)
    {
        test::Section("the same words on two panes");

        // Read out of the real language screen in Pokemon X, which holds
        // "Play Pokemon X in" twice -- once per display line. Saying it twice
        // tells the player nothing and costs them time.
        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);

        std::vector<Observation> obs = Furniture();
        obs.push_back(Observation(0x08500000, "Play Pokemon X in"));
        obs.push_back(Observation(0x08500100, "Play Pokemon X in"));

        Settle(n, obs, spoken);
        test::Equal(spoken.size(), 1u, "spoken once, not twice");
        if (spoken.size() == 1)
            test::EqualStr(spoken[0], "Play Pokemon X in", "and it is the right line");
    }

    void TestReadAllDeduplicates(void)
    {
        test::Section("read-screen with repeated text");

        narration::Narrator n;
        std::vector<Observation> obs = Furniture();
        obs.push_back(Observation(0x08500000, "Play Pokemon X in"));
        obs.push_back(Observation(0x08500100, "Play Pokemon X in"));

        std::vector<std::string> all;
        n.ReadAll(obs, all);
        test::Equal(all.size(), 6u, "five labels plus one copy of the repeat");
    }

    void TestDistinctTextStillBothSpoken(void)
    {
        test::Section("two panes that differ");

        // Deduplication must not silence a genuinely different line.
        narration::Narrator n;
        std::vector<std::string> spoken;
        Settle(n, Furniture(), spoken);

        std::vector<Observation> obs = Furniture();
        obs.push_back(Observation(0x08500000, "Play Pokemon X in"));
        obs.push_back(Observation(0x08500100, "Play Pokemon Y in"));

        Settle(n, obs, spoken);
        test::Equal(spoken.size(), 2u, "both are spoken");
    }
}

int main(void)
{
    std::printf("\nchoosing what to narrate\n========================\n");

    TestBaselineIsSilent();
    TestChangeIsSpoken();
    TestStaticFurnitureStaysSilent();
    TestChangedLabelIsSpoken();
    TestJunkIsFiltered();
    TestDisappearedPanesAreForgotten();
    TestReappearingTextSpeaksAgain();
    TestNewContextResets();
    TestReadAllIgnoresHistory();
    TestEmptyPollDoesNotWasteBaseline();
    TestDuplicatePanesSpeakOnce();
    TestReadAllDeduplicates();
    TestDistinctTextStillBothSpoken();

    return test::Report("narration");
}
