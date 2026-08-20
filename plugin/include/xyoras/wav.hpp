/*
 * XYORAS Access — RIFF/WAVE header construction.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Header-only and free of 3DS dependencies, so the host tests exercise the
 * same code the plugin ships rather than a copy of it.
 */
#ifndef XYORAS_WAV_HPP
#define XYORAS_WAV_HPP

#include "xyoras/common.hpp"

#include <cstring>

namespace xyoras { namespace wav {

    /// Canonical 44-byte RIFF/WAVE header for uncompressed mono PCM16.
    /// Field order and sizes are fixed by the format; do not reorder.
    struct Header
    {
        char riff[4];        u32 riffSize;
        char wave[4];
        char fmt[4];         u32 fmtSize;
        u16  audioFormat;    u16 channels;
        u32  sampleRate;     u32 byteRate;
        u16  blockAlign;     u16 bitsPerSample;
        char data[4];        u32 dataSize;
    };

    /// Fills a header for `samples` mono PCM16 samples at `sampleRate`.
    inline void Fill(Header &h, u32 samples, u32 sampleRate)
    {
        const u32 dataBytes = samples * sizeof(s16);

        std::memcpy(h.riff, "RIFF", 4);
        std::memcpy(h.wave, "WAVE", 4);
        std::memcpy(h.fmt,  "fmt ", 4);
        std::memcpy(h.data, "data", 4);

        // riffSize counts everything after this field: the 4-byte "WAVE" tag,
        // the 8+16-byte fmt chunk, and the 8-byte data chunk header, plus the
        // samples. That is 36 bytes of overhead.
        h.riffSize      = 36 + dataBytes;
        h.fmtSize       = 16;   // size of a PCM fmt chunk
        h.audioFormat   = 1;    // 1 == uncompressed PCM
        h.channels      = 1;
        h.sampleRate    = sampleRate;
        h.bitsPerSample = 16;
        h.blockAlign    = static_cast<u16>(h.channels * (h.bitsPerSample / 8));
        h.byteRate      = h.sampleRate * h.blockAlign;
        h.dataSize      = dataBytes;
    }

}} // namespace xyoras::wav

#endif
