/*
 * XYORAS Access — internals shared between the speech queue and the worker.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Not part of the public speech interface. Feature code includes
 * "xyoras/speech.hpp" and never this.
 */
#ifndef XYORAS_SPEECH_INTERNAL_HPP
#define XYORAS_SPEECH_INTERNAL_HPP

#include "xyoras/speech.hpp"
#include "xyoras/sync.hpp"

#include <vector>

namespace xyoras { namespace speech {

    struct Item
    {
        Priority    priority;
        std::string text;

        Item(void) : priority(Priority::Ui) {}
    };

    /// Priority queue with the interruption and coalescing policy from
    /// "AI docks/02-accessibility-design.md". All methods are thread-safe:
    /// producers are game threads, the consumer is the synthesis worker.
    class Queue
    {
    public:
        Queue(void) : cancelCurrent_(false) {}

        /// Applies the policy, then enqueues. Never blocks on synthesis.
        void Push(Priority priority, const std::string &text);

        /// Removes and returns the highest-priority item. False if empty.
        bool Pop(Item &out);

        void Clear(void);

        /// True once per request to abandon the utterance being spoken.
        bool TakeCancelRequest(void);

        bool Empty(void);

        void WaitForWork(u64 timeoutNs) { event_.Wait(timeoutNs); }

    private:
        Mutex                     mutex_;
        WakeEvent                 event_;
        std::vector<Item>         items_;
        bool                      cancelCurrent_;
    };

}} // namespace xyoras::speech

#endif
