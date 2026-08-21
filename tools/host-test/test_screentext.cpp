/*
 * XYORAS Access — host tests for deciding when text is worth speaking.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The case that dominates this file is the typing animation. Gen 6 reveals
 * dialogue one character at a time, so a mod that speaks on every change would
 * stutter the opening syllable of every line dozens of times. These tests
 * simulate that animation directly and require the line to be spoken once,
 * whole, at the end.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/screentext.hpp"

#include <string>
#include <vector>

using namespace xyoras;

namespace {

    /// Feeds a tracker a sequence of polls and collects everything it reports.
    std::vector<std::string> Feed(screentext::Tracker &t,
                                  const std::vector<std::string> &polls)
    {
        std::vector<std::string> spoken;
        for (u32 i = 0; i < polls.size(); ++i)
        {
            std::string out;
            if (t.Update(polls[i], out))
                spoken.push_back(out);
        }
        return spoken;
    }

    /// The character-by-character reveal, then the finished line held while the
    /// player reads it.
    std::vector<std::string> TypeOut(const std::string &line, u32 holdPolls)
    {
        std::vector<std::string> polls;
        for (u32 i = 1; i <= line.size(); ++i)
            polls.push_back(line.substr(0, i));
        for (u32 i = 0; i < holdPolls; ++i)
            polls.push_back(line);
        return polls;
    }

    void TestTypingAnimationSpeaksOnce(void)
    {
        test::Section("a line typed out is spoken once, whole");

        screentext::Tracker t;
        const std::string line = "You got a message!";
        const std::vector<std::string> spoken = Feed(t, TypeOut(line, 10));

        test::Equal(spoken.size(), 1u, "spoken exactly once, not once per character");
        if (spoken.size() == 1)
            test::EqualStr(spoken[0], line, "and the whole line, not a fragment");
    }

    void TestHoldingDoesNotRepeat(void)
    {
        test::Section("text left on screen is not repeated");

        // A player reading slowly leaves the box up for hundreds of polls.
        screentext::Tracker t;
        std::vector<std::string> polls;
        for (u32 i = 0; i < 500; ++i)
            polls.push_back("Press A to continue.");

        const std::vector<std::string> spoken = Feed(t, polls);
        test::Equal(spoken.size(), 1u, "500 identical polls speak once");
    }

    void TestSuccessiveLines(void)
    {
        test::Section("a conversation");

        screentext::Tracker t;
        std::vector<std::string> polls;
        const char *lines[] = {"Hello there!", "How are you?", "Goodbye."};
        for (u32 i = 0; i < 3; ++i)
        {
            const std::vector<std::string> typed = TypeOut(lines[i], 6);
            polls.insert(polls.end(), typed.begin(), typed.end());
        }

        const std::vector<std::string> spoken = Feed(t, polls);
        test::Equal(spoken.size(), 3u, "three lines, three utterances");
        if (spoken.size() == 3)
        {
            test::EqualStr(spoken[0], "Hello there!", "first line");
            test::EqualStr(spoken[1], "How are you?", "second line");
            test::EqualStr(spoken[2], "Goodbye.", "third line");
        }
    }

    void TestRepeatedLineAfterClearing(void)
    {
        test::Section("the same line again after the box closes");

        // Reading the same sign twice must speak it twice. Remembering it
        // forever would leave the player wondering if the mod had failed.
        screentext::Tracker t;
        std::vector<std::string> polls = TypeOut("A signpost.", 5);
        polls.push_back("");                       // box closes
        polls.push_back("");
        const std::vector<std::string> again = TypeOut("A signpost.", 5);
        polls.insert(polls.end(), again.begin(), again.end());

        const std::vector<std::string> spoken = Feed(t, polls);
        test::Equal(spoken.size(), 2u, "read twice, spoken twice");
    }

    void TestFlickerDoesNotSpeakTwice(void)
    {
        test::Section("text that never leaves is not re-spoken");

        // Distinct from the case above: without an empty poll in between, the
        // same text must not be repeated.
        screentext::Tracker t;
        std::vector<std::string> polls = TypeOut("Static label", 5);
        for (u32 i = 0; i < 20; ++i)
            polls.push_back("Static label");

        test::Equal(Feed(t, polls).size(), 1u, "still only once");
    }

    void TestSettleThreshold(void)
    {
        test::Section("nothing is spoken before it settles");

        // One poll of stability is not enough: the animation pauses briefly
        // between characters, and speaking then would produce fragments.
        screentext::Tracker t;
        std::string out;

        test::Check(!t.Update("You g", out), "first sighting says nothing");
        test::Check(!t.Update("You g", out), "one stable poll is not enough");

        // With kSettlePolls == 3 the third stable poll is the one that fires.
        bool fired = false;
        for (u32 i = 0; i < screentext::kSettlePolls; ++i)
            if (t.Update("You g", out))
                fired = true;
        test::Check(fired, "it does fire once stable long enough");
    }

    void TestEmptyClears(void)
    {
        test::Section("clearing");

        screentext::Tracker t;
        std::string out;
        Feed(t, TypeOut("Something", 5));
        test::EqualStr(t.LastSpoken(), "Something", "remembers what it said");

        test::Check(!t.Update("", out), "an empty poll reports nothing");
        test::EqualStr(t.LastSpoken(), "", "and forgets");
    }

    void TestReset(void)
    {
        test::Section("explicit reset");

        // Entering a battle or closing a menu changes context entirely; old
        // text must not suppress identical new text.
        screentext::Tracker t;
        Feed(t, TypeOut("Ready?", 5));
        t.Reset();
        test::EqualStr(t.LastSpoken(), "", "reset forgets");

        const std::vector<std::string> spoken = Feed(t, TypeOut("Ready?", 5));
        test::Equal(spoken.size(), 1u, "the same text speaks again after a reset");
    }

    void TestIndependentTrackers(void)
    {
        test::Section("separate trackers do not interfere");

        // A busy menu must not be able to suppress a story line.
        screentext::Tracker dialogue, menu;
        std::string out;

        const std::vector<std::string> d = Feed(dialogue, TypeOut("Story line.", 5));
        const std::vector<std::string> m = Feed(menu, TypeOut("Potion", 5));

        test::Equal(d.size(), 1u, "dialogue spoken");
        test::Equal(m.size(), 1u, "menu entry spoken");
        test::EqualStr(dialogue.LastSpoken(), "Story line.", "each keeps its own state");
        test::EqualStr(menu.LastSpoken(), "Potion", "independently");
    }

    void TestIsContinuation(void)
    {
        test::Section("telling a typing animation from a new line");

        test::Check(screentext::IsContinuationOf("You g", "You got a message"),
                    "a longer string with the same start is a continuation");
        test::Check(!screentext::IsContinuationOf("Hello", "Goodbye"),
                    "a different line is not");
        test::Check(!screentext::IsContinuationOf("Same", "Same"),
                    "identical is not a continuation");
        test::Check(!screentext::IsContinuationOf("Longer text", "Short"),
                    "shorter is not a continuation");
        test::Check(!screentext::IsContinuationOf("", "Anything"),
                    "an empty start is not a continuation");
    }
}

int main(void)
{
    std::printf("\nwhen to speak on-screen text\n============================\n");

    TestTypingAnimationSpeaksOnce();
    TestHoldingDoesNotRepeat();
    TestSuccessiveLines();
    TestRepeatedLineAfterClearing();
    TestFlickerDoesNotSpeakTwice();
    TestSettleThreshold();
    TestEmptyClears();
    TestReset();
    TestIndependentTrackers();
    TestIsContinuation();

    return test::Report("screentext");
}
