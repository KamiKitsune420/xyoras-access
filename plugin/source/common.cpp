/*
 * XYORAS Access — shared helpers.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 */
#include "xyoras/common.hpp"

namespace xyoras
{
    using CTRPluginFramework::File;

    namespace { bool g_logEnabled = false; }

    void SetLogEnabled(bool enabled) { g_logEnabled = enabled; }

    void Log(const std::string &message)
    {
        // Off by default and deliberately so: SD writes are slow enough to be
        // felt, and this can be called from paths that run every frame.
        if (!g_logEnabled)
            return;

        File log;
        if (File::Open(log, std::string(kSdRoot) + "/log.txt",
                       File::RW | File::CREATE | File::APPEND) != 0)
            return;

        log.WriteLine(message);
        log.Close();
    }
}
