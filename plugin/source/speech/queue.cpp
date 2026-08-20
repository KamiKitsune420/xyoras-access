/*
 * XYORAS Access — the speech queue and its policy.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The interruption and coalescing rules from "AI docks/02-accessibility-design.md"
 * are implemented here and nowhere else.
 */
#include "xyoras/speech.hpp"
#include "speech_internal.hpp"

#include <algorithm>

namespace xyoras { namespace speech {

namespace {
    /// Beyond this the player is not listening any more, they are waiting.
    /// Dropping the oldest Ambient/Ui item is better than a growing backlog.
    constexpr size_t kMaxPending = 32;

    bool IsHigherPriority(Priority a, Priority b)
    {
        return static_cast<int>(a) < static_cast<int>(b);
    }
}

void Queue::Push(Priority priority, const std::string &text)
{
    if (text.empty())
        return;

    Lock lock(mutex_);

    // Interrupt clears the board. The player asked for something now.
    if (priority == Priority::Interrupt)
    {
        items_.clear();
        cancelCurrent_ = true;
    }
    // Critical must be heard now, so it clears the chatter -- but deliberately
    // NOT Dialogue.
    //
    // "Critical cancels lower priorities" and "Dialogue is never dropped" are
    // both design rules, and Dialogue is the lower of the two, so they collide
    // here. Dialogue wins: a fainting message must not silently delete the
    // story line queued behind it. The player did not ask for that trade, and
    // a lost line is unrecoverable except through message history.
    //
    // Nothing is lost by keeping it. The priority sort already places this
    // utterance ahead of pending Dialogue, and cancelCurrent_ stops whatever
    // is being spoken right now -- so Critical is still heard immediately, and
    // the story simply resumes afterwards.
    else if (priority == Priority::Critical)
    {
        items_.erase(
            std::remove_if(items_.begin(), items_.end(),
                [](const Item &i) { return i.priority == Priority::Ui
                                        || i.priority == Priority::Ambient; }),
            items_.end());
        cancelCurrent_ = true;
    }
    // A new Ui utterance replaces a pending one rather than queueing behind
    // it, so holding a direction on a menu cannot build a speech backlog.
    else if (priority == Priority::Ui)
    {
        items_.erase(
            std::remove_if(items_.begin(), items_.end(),
                [](const Item &i) { return i.priority == Priority::Ui; }),
            items_.end());
    }
    // Ambient is the lowest form of speech: if anything else is waiting, the
    // player does not need to hear a step tick described.
    else if (priority == Priority::Ambient)
    {
        if (!items_.empty())
            return;
    }

    if (items_.size() >= kMaxPending)
    {
        // Drop the lowest-priority pending item, never a Dialogue line —
        // losing a story line loses the plot.
        auto victim = items_.end();
        for (auto it = items_.begin(); it != items_.end(); ++it)
        {
            if (it->priority == Priority::Dialogue)
                continue;
            if (victim == items_.end() || IsHigherPriority(victim->priority, it->priority))
                victim = it;
        }
        if (victim != items_.end())
            items_.erase(victim);
        else
            return;     // all Dialogue: keep them all, drop the newcomer
    }

    Item item;
    item.priority = priority;
    item.text     = text;
    items_.push_back(item);

    // Stable sort by priority keeps arrival order within a priority class,
    // which is what makes Dialogue read in the order the game produced it.
    std::stable_sort(items_.begin(), items_.end(),
        [](const Item &a, const Item &b) { return IsHigherPriority(a.priority, b.priority); });

    event_.Signal();
}

bool Queue::Pop(Item &out)
{
    Lock lock(mutex_);

    if (items_.empty())
        return false;

    out = items_.front();
    items_.erase(items_.begin());
    return true;
}

void Queue::Clear(void)
{
    Lock lock(mutex_);
    items_.clear();
    cancelCurrent_ = true;
    event_.Signal();
}

bool Queue::TakeCancelRequest(void)
{
    Lock lock(mutex_);
    const bool wanted = cancelCurrent_;
    cancelCurrent_ = false;
    return wanted;
}

bool Queue::Empty(void)
{
    Lock lock(mutex_);
    return items_.empty();
}

}} // namespace xyoras::speech
