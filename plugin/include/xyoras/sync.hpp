/*
 * XYORAS Access — threading primitives, portable across target and host.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The plugin cannot be run in an emulator (see "AI docks/10-testing-and-qa.md"),
 * so the only way to test its logic without a console is to compile that logic
 * natively and run it on a PC. This header is what makes that possible: the
 * queue and its policy are built from these types rather than from CTRPF's
 * directly, so the SAME source file is exercised by the host tests and shipped
 * in the plugin.
 *
 * Test a copy of the code and you have tested nothing.
 */
#ifndef XYORAS_SYNC_HPP
#define XYORAS_SYNC_HPP

#ifdef __3DS__
  #include <CTRPluginFramework.hpp>
  #include <3ds.h>
#else
  #include <chrono>
  #include <condition_variable>
  #include <mutex>
#endif

#include "xyoras/common.hpp"

namespace xyoras
{

#ifdef __3DS__

    typedef CTRPluginFramework::Mutex Mutex;
    typedef CTRPluginFramework::Lock  Lock;

    /// Lets the synthesis worker sleep until there is work rather than spin.
    /// The wait is timed so a shutdown request is still noticed promptly when
    /// the queue stays empty.
    class WakeEvent
    {
    public:
        WakeEvent(void)          { LightEvent_Init(&event_, RESET_ONESHOT); }
        void Signal(void)        { LightEvent_Signal(&event_); }
        void Wait(u64 timeoutNs) { LightEvent_WaitTimeout(&event_, timeoutNs); }

    private:
        LightEvent event_;
    };

#else   // host build, for tests

    typedef std::mutex                   Mutex;
    typedef std::lock_guard<std::mutex>  Lock;

    class WakeEvent
    {
    public:
        WakeEvent(void) : signalled_(false) {}

        void Signal(void)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                signalled_ = true;
            }
            cv_.notify_one();
        }

        void Wait(u64 timeoutNs)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::nanoseconds(timeoutNs),
                         [this] { return signalled_; });
            signalled_ = false;
        }

    private:
        std::mutex              mutex_;
        std::condition_variable cv_;
        bool                    signalled_;
    };

#endif

}

#endif
