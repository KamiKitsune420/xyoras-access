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

#include <CTRPluginFramework.hpp>
#include <string>

namespace xyoras
{
    // CTRPF's types.h puts these in the global namespace, not in
    // CTRPluginFramework. Re-export them under xyoras:: so our own headers do
    // not have to reach into the global namespace everywhere.
    typedef ::u8  u8;
    typedef ::u16 u16;
    typedef ::u32 u32;
    typedef ::u64 u64;
    typedef ::s8  s8;
    typedef ::s16 s16;
    typedef ::s32 s32;
    typedef ::s64 s64;

    /// Version of this plugin, reported in the settings menu.
    constexpr const char *kVersion = "0.1.0";

    /// Root of our data on the SD card. Voice data, sounds, config, log.
    constexpr const char *kSdRoot = "/xyoras-access";

    /// Written only when Config::debugLog is on — SD writes are slow.
    void Log(const std::string &message);
}

#endif
