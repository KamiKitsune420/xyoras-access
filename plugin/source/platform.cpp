/*
 * XYORAS Access — process-level setup a plugin has to do for itself.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 */
#include "xyoras/common.hpp"
#include "xyoras/platform.hpp"

#include <3ds.h>
#include <3ds/archive.h>

namespace xyoras { namespace platform {

namespace {
    bool g_sdmcMounted = false;
}

bool MountSdmc(void)
{
    if (g_sdmcMounted)
        return true;

    // A homebrew application gets this for free: libctru's startup code
    // registers the "sdmc:" devoptab before main() runs, so stdio can resolve
    // paths. A plugin is injected into an already-running game and never goes
    // through that path, so stdio has no idea what "sdmc:" means and every
    // fopen fails.
    //
    // That matters enormously here, because eSpeak reads ALL of its voice data
    // (phontab, phondata, phonindex, en_dict) through stdio. Without this call
    // eSpeak cannot load anything -- and it does not fail cleanly when it
    // can't, it hangs inside espeak_Initialize and takes the plugin with it.
    //
    // Measured, not assumed: fopen fails before this call and succeeds after.
    // See "AI docks/12-research-log.md".
    fsInit();

    if (R_FAILED(archiveMountSdmc()))
        return false;

    g_sdmcMounted = true;
    return true;
}

bool IsSdmcMounted(void)
{
    return g_sdmcMounted;
}

}} // namespace xyoras::platform
