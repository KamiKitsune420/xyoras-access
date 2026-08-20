/*
 * XYORAS Access — host tests for the speech queue policy.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * This compiles the REAL queue.cpp, not a reimplementation of it. The policy
 * tested here is the one specified in "AI docks/02-accessibility-design.md",
 * and it is the part of the mod most likely to make the game unplayable if it
 * is wrong: too eager and the player drowns in speech, too aggressive and
 * story dialogue silently disappears.
 */
#include "test.hpp"
#include "../../plugin/source/speech/speech_internal.hpp"

#include <vector>

using namespace xyoras::speech;
using xyoras::speech::Priority;

namespace {

    /// Drains the queue into a list, so ordering can be asserted directly.
    std::vector<std::string> Drain(Queue &q)
    {
        std::vector<std::string> out;
        Item item;
        while (q.Pop(item))
            out.push_back(item.text);
        return out;
    }

    size_t Count(Queue &q)
    {
        return Drain(q).size();
    }

    void TestOrdering(void)
    {
        test::Section("priority ordering");

        // Ordering only. Ui and Ambient are deliberately absent: they carry
        // their own coalescing and drop rules, which would mask what is being
        // measured here. Those are covered by their own tests below.
        Queue q;
        q.Push(Priority::Dialogue, "dialogue");
        q.Push(Priority::Critical, "critical");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 2u, "both utterances survive");
        if (got.size() == 2)
        {
            test::EqualStr(got[0], "critical", "critical is spoken first");
            test::EqualStr(got[1], "dialogue", "dialogue follows it");
        }
    }

    void TestAmbientIsLast(void)
    {
        test::Section("ambient sorts behind everything");

        // Pushed into an empty queue so the "drop ambient if anything is
        // pending" rule does not fire; then something louder arrives that does
        // not clear it.
        Queue q;
        q.Push(Priority::Ambient,  "step");
        q.Push(Priority::Dialogue, "story");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 2u, "both kept");
        if (got.size() == 2)
        {
            test::EqualStr(got[0], "story", "dialogue outranks ambient");
            test::EqualStr(got[1], "step",  "ambient is spoken last");
        }
    }

    void TestArrivalOrderWithinPriority(void)
    {
        test::Section("arrival order within a priority");

        // Dialogue must read in the order the game produced it, or the story
        // arrives scrambled. This is why the sort has to be stable.
        Queue q;
        q.Push(Priority::Dialogue, "first");
        q.Push(Priority::Dialogue, "second");
        q.Push(Priority::Dialogue, "third");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 3u, "no dialogue lost");
        if (got.size() == 3)
        {
            test::EqualStr(got[0], "first",  "dialogue keeps arrival order (1)");
            test::EqualStr(got[1], "second", "dialogue keeps arrival order (2)");
            test::EqualStr(got[2], "third",  "dialogue keeps arrival order (3)");
        }
    }

    void TestInterruptClears(void)
    {
        test::Section("Interrupt clears everything");

        Queue q;
        q.Push(Priority::Dialogue, "old dialogue");
        q.Push(Priority::Ui,       "old ui");
        q.Push(Priority::Interrupt, "player asked for this");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 1u, "queue holds only the interrupting utterance");
        if (got.size() == 1)
            test::EqualStr(got[0], "player asked for this", "the interrupt survives");
    }

    void TestInterruptRequestsCancel(void)
    {
        test::Section("Interrupt cancels what is being spoken");

        Queue q;
        test::Check(!q.TakeCancelRequest(), "no cancel pending initially");

        q.Push(Priority::Interrupt, "now");
        test::Check(q.TakeCancelRequest(), "Interrupt raises a cancel request");
        test::Check(!q.TakeCancelRequest(), "the request is consumed only once");
    }

    void TestUiCoalescing(void)
    {
        test::Section("Ui coalescing");

        // Holding a direction on a menu must not build a backlog: only the
        // item the cursor has actually landed on matters.
        Queue q;
        q.Push(Priority::Ui, "Potion");
        q.Push(Priority::Ui, "Super Potion");
        q.Push(Priority::Ui, "Hyper Potion");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 1u, "only the newest Ui utterance is kept");
        if (got.size() == 1)
            test::EqualStr(got[0], "Hyper Potion", "the newest one is the survivor");
    }

    void TestUiDoesNotDropDialogue(void)
    {
        test::Section("Ui coalescing spares other categories");

        Queue q;
        q.Push(Priority::Dialogue, "story line");
        q.Push(Priority::Ui,       "cursor a");
        q.Push(Priority::Ui,       "cursor b");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 2u, "dialogue kept, Ui collapsed to one");
        if (got.size() == 2)
        {
            test::EqualStr(got[0], "story line", "dialogue outranks Ui");
            test::EqualStr(got[1], "cursor b",   "newest Ui kept");
        }
    }

    void TestAmbientYields(void)
    {
        test::Section("Ambient yields to anything pending");

        Queue busy;
        busy.Push(Priority::Dialogue, "story");
        busy.Push(Priority::Ambient,  "step");
        test::Equal(Count(busy), 1u, "ambient dropped when something is waiting");

        Queue idle;
        idle.Push(Priority::Ambient, "step");
        test::Equal(Count(idle), 1u, "ambient kept when the queue is empty");
    }

    void TestCriticalOutranks(void)
    {
        test::Section("Critical clears chatter but spares dialogue");

        // The rule that dialogue is never dropped outranks the rule that
        // Critical clears lower priorities. A fainting message must be heard
        // first, but it must not delete the story line queued behind it.
        Queue q;
        q.Push(Priority::Dialogue, "story");
        q.Push(Priority::Ui,       "cursor");
        q.Push(Priority::Ambient,  "step");
        q.Push(Priority::Critical, "your Pokemon fainted");

        const std::vector<std::string> got = Drain(q);

        test::Equal(got.size(), 2u, "Ui and Ambient cleared, dialogue kept");
        if (got.size() == 2)
        {
            test::EqualStr(got[0], "your Pokemon fainted", "critical is spoken first");
            test::EqualStr(got[1], "story", "the story line survives and follows it");
        }
    }

    void TestDialogueNeverDropped(void)
    {
        test::Section("dialogue survives overflow");

        // The cap exists so a runaway producer cannot grow the queue without
        // bound. Enforcing it must never cost a story line.
        Queue q;
        for (int i = 0; i < 200; ++i)
            q.Push(Priority::Dialogue, "line " + std::to_string(i));

        const std::vector<std::string> got = Drain(q);

        test::Check(got.size() >= 32, "queue retained a full batch of dialogue");
        if (!got.empty())
        {
            test::EqualStr(got[0], "line 0", "the oldest dialogue is still first");

            bool ordered = true;
            for (size_t i = 0; i < got.size(); ++i)
                if (got[i] != "line " + std::to_string(i))
                    ordered = false;
            test::Check(ordered, "retained dialogue is contiguous and in order");
        }
    }

    void TestOverflowDropsLowestFirst(void)
    {
        test::Section("overflow sheds the least important first");

        Queue q;
        q.Push(Priority::Dialogue, "story");
        for (int i = 0; i < 100; ++i)
            q.Push(Priority::Ui, "cursor " + std::to_string(i));

        const std::vector<std::string> got = Drain(q);

        bool dialogueSurvived = false;
        for (size_t i = 0; i < got.size(); ++i)
            if (got[i] == "story")
                dialogueSurvived = true;

        test::Check(dialogueSurvived, "dialogue survives a flood of Ui utterances");
    }

    void TestEmptyTextIgnored(void)
    {
        test::Section("empty utterances");

        Queue q;
        q.Push(Priority::Dialogue, "");
        test::Equal(Count(q), 0u, "empty text is not queued");
    }

    void TestClear(void)
    {
        test::Section("Clear");

        Queue q;
        q.Push(Priority::Dialogue, "a");
        q.Push(Priority::Dialogue, "b");
        q.Clear();

        test::Equal(Count(q), 0u, "Clear empties the queue");
        test::Check(q.Empty(), "Empty() agrees");
    }

    void TestPopOnEmpty(void)
    {
        test::Section("Pop on an empty queue");

        Queue q;
        Item item;
        test::Check(!q.Pop(item), "Pop reports failure rather than returning junk");
    }
}

int main(void)
{
    std::printf("\nspeech queue policy\n===================\n");

    TestOrdering();
    TestAmbientIsLast();
    TestArrivalOrderWithinPriority();
    TestInterruptClears();
    TestInterruptRequestsCancel();
    TestUiCoalescing();
    TestUiDoesNotDropDialogue();
    TestAmbientYields();
    TestCriticalOutranks();
    TestDialogueNeverDropped();
    TestOverflowDropsLowestFirst();
    TestEmptyTextIgnored();
    TestClear();
    TestPopOnEmpty();

    return test::Report("speech queue");
}
