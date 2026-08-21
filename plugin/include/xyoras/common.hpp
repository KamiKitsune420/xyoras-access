/*
 * XYORAS Access — accessibility mod for Pokemon Generation 6 on Nintendo 3DS
 * Copyright (C) 2026 XYORAS Access contributors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See LICENSE for the full text.
 */
#ifndef XYORAS_COMMON_HPP
#define XYORAS_COMMON_HPP

// Parts of this codebase are compiled twice: for the plugin, and natively for
// the host tests. Anything that only exists on the 3DS has to be guarded, or
// the host build cannot see the logic it is meant to be testing.
#ifdef __3DS__
  #include <CTRPluginFramework.hpp>
#endif

#include <cstdint>
#include <string>

namespace xyoras
{
    // Fixed-width types, spelled the way the 3DS homebrew world spells them.
    // Defined from <cstdint> rather than taken from CTRPF's types.h so that
    // they mean the same thing in a host build.
    typedef std::uint8_t  u8;
    typedef std::uint16_t u16;
    typedef std::uint32_t u32;
    typedef std::uint64_t u64;
    typedef std::int8_t   s8;
    typedef std::int16_t  s16;
    typedef std::int32_t  s32;
    typedef std::int64_t  s64;

    /// Version of this plugin, reported in the settings menu.
    constexpr const char *kVersion = "0.1.0";

    /// Root of our data on the SD card. Voice data, sounds, config, log.
    constexpr const char *kSdRoot = "/xyoras-access";

    /// Written only when debug logging is on — SD writes are slow enough to be
    /// felt, and this can be reached from paths that run every frame.
    void Log(const std::string &message);
    void SetLogEnabled(bool enabled);

    namespace diag
    {
        /// Appends a line to /xyoras-access/checkpoints.txt through CTRPF's
        /// File API. Startup instrumentation only -- it opens and closes the
        /// file on every call, so it is far too slow for anything per-frame.
        void Checkpoint(const char *stage);
    }
}

#endif
