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

namespace {
    /// svcControlMemory works in pages and rejects anything else.
    inline u32 PageAlign(u32 size)
    {
        return (size + 0xFFF) & ~0xFFFu;
    }
}

void *LinearAlloc(u32 size)
{
    if (size == 0)
        return nullptr;

    // libctru's linearAlloc draws from a heap that __appInit creates. A plugin
    // is injected into a running game and never executes that, so linearAlloc
    // returns null every time -- measured, see "AI docks/12-research-log.md".
    //
    // MEMOP_ALLOC_LINEAR asks the kernel directly and is what libctru's own
    // heap setup uses underneath.
    u32 addr = 0;
    const Result res = svcControlMemory(&addr, 0, 0, PageAlign(size),
                                        MEMOP_ALLOC_LINEAR, MEMPERM_READWRITE);

    if (R_FAILED(res) || addr == 0)
        return nullptr;

    return reinterpret_cast<void *>(addr);
}

void LinearFree(void *ptr, u32 size)
{
    if (ptr == nullptr || size == 0)
        return;

    u32 tmp = 0;
    svcControlMemory(&tmp, reinterpret_cast<u32>(ptr), 0, PageAlign(size),
                     MEMOP_FREE, MEMPERM_DONTCARE);
}

}} // namespace xyoras::platform
