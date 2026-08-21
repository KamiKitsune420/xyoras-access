/*
 * XYORAS Access — written diagnostics.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * A blind player cannot read a crash screen, an on-screen log, or a debugger.
 * When something does not get spoken, the only evidence anyone can act on is a
 * file on the SD card that they can send. Everything here writes one.
 *
 * All of it is off unless a marker file asks for it, so an ordinary player
 * never pays for it.
 */
#ifndef XYORAS_DIAGNOSTICS_HPP
#define XYORAS_DIAGNOSTICS_HPP

#include "xyoras/common.hpp"

namespace xyoras { namespace diag {

    /// Marker files on the SD card, all under /xyoras-access/.
    bool IsSelfTestRequested(void);
    bool IsWavDumpRequested(void);

    /// Records how far startup got. Written unconditionally, so if the plugin
    /// runs at all this file appears.
    void Checkpoint(const char *stage);

    /// Does plain fopen work in a game process? eSpeak loads its voice data
    /// that way, so this answers whether speech can work at all.
    void ProbeFopen(const char *when);

    void RunSelfTest(void);

    /// Narration tracing: what the scan found and what got spoken.
    ///
    /// Enabled by /xyoras-access/trace-narration. This is the log that answers
    /// "why did it not read that sign" -- whether the scan found no panes,
    /// found them but read nothing, or read the text and chose not to say it.
    bool IsNarrationTraceRequested(void);
    void NarrationTrace(const std::string &line);

}} // namespace xyoras::diag

#endif
