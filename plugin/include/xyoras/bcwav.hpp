/*
 * XYORAS Access — building a BCWAV container around raw PCM16.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * eSpeak produces raw PCM16. libcwav plays BCWAV. This turns one into the
 * other in memory, so synthesised speech never has to touch the SD card.
 *
 * The layout below is not invented: every field and magic value here is what
 * libcwav's own parser checks for (see its cwav_defs.h and cwav.c). Get one
 * wrong and cwavLoad reports UNKNOWN_FILE_FORMAT or INVAID_INFO_BLOCK.
 *
 * Header-only and free of 3DS dependencies, so the host tests can verify the
 * bytes without a console.
 */
#ifndef XYORAS_BCWAV_HPP
#define XYORAS_BCWAV_HPP

#include "xyoras/common.hpp"

#include <cstring>

namespace xyoras { namespace bcwav {

    // Magic values, little-endian, exactly as libcwav compares them.
    constexpr u32 kMagicCwav = 0x56415743;  // "CWAV"
    constexpr u32 kMagicInfo = 0x4F464E49;  // "INFO"
    constexpr u32 kMagicData = 0x41544144;  // "DATA"

    constexpr u16 kEndian  = 0xFEFF;
    constexpr u32 kVersion = 0x02010000;    // libcwav accepts this exact value
    constexpr u16 kBlockCount = 2;          // INFO + DATA, and it checks

    // Reference types from libcwav's cwavReferenceType_t.
    constexpr u16 kRefInfoBlock   = 0x7000;
    constexpr u16 kRefDataBlock   = 0x7001;
    constexpr u16 kRefChannelInfo = 0x7100;
    constexpr u16 kRefSampleData  = 0x1F00;

    /// Encoding ids. PCM16 is the one we use: no encode step, and it is valid
    /// on both the CSND and DSP backends.
    constexpr u8 kEncodingPcm8  = 0;
    constexpr u8 kEncodingPcm16 = 1;

    /// Fixed offsets in the file we generate. Blocks are 0x20-aligned, which
    /// is what real BCWAV files do and keeps the sample data comfortably
    /// aligned for the audio hardware.
    constexpr u32 kHeaderSize = 0x40;   // padded; real files use 0x40 too
    constexpr u32 kInfoOffset = 0x40;
    constexpr u32 kInfoSize   = 0x40;   // 60 bytes of content, padded
    constexpr u32 kDataOffset = kInfoOffset + kInfoSize;   // 0x80
    constexpr u32 kDataHeaderSize = 8;

    /// Total bytes needed for `samples` mono PCM16 samples.
    inline u32 FileSize(u32 samples)
    {
        return kDataOffset + kDataHeaderSize + samples * sizeof(s16);
    }

    /// Where the caller should copy the PCM, relative to the buffer start.
    inline u32 PcmOffset(void)
    {
        return kDataOffset + kDataHeaderSize;
    }

    namespace detail {
        inline void Put16(u8 *p, u32 off, u16 v) { std::memcpy(p + off, &v, 2); }
        inline void Put32(u8 *p, u32 off, u32 v) { std::memcpy(p + off, &v, 4); }
    }

    /// Writes the container around a PCM payload the caller places at
    /// PcmOffset(). `buffer` must hold at least FileSize(samples) bytes.
    ///
    /// Mono only: one channel, one channel-info entry. That is all speech
    /// needs, and it keeps the reference arithmetic simple enough to verify.
    inline void Build(u8 *buffer, u32 samples, u32 sampleRate)
    {
        using namespace detail;

        const u32 pcmBytes  = samples * sizeof(s16);
        const u32 dataSize  = kDataHeaderSize + pcmBytes;

        std::memset(buffer, 0, kDataOffset + kDataHeaderSize);

        // ---- File header -------------------------------------------------
        Put32(buffer, 0x00, kMagicCwav);
        Put16(buffer, 0x04, kEndian);
        Put16(buffer, 0x06, static_cast<u16>(kHeaderSize));
        Put32(buffer, 0x08, kVersion);
        Put32(buffer, 0x0C, FileSize(samples));
        Put16(buffer, 0x10, kBlockCount);
        // 0x12: reserved u16, already zero

        // info_blck: sized reference at 0x14 { refType, pad, offset, size }
        Put16(buffer, 0x14, kRefInfoBlock);
        Put32(buffer, 0x18, kInfoOffset);
        Put32(buffer, 0x1C, kInfoSize);

        // data_blck: sized reference at 0x20
        Put16(buffer, 0x20, kRefDataBlock);
        Put32(buffer, 0x24, kDataOffset);
        Put32(buffer, 0x28, dataSize);

        // ---- INFO block --------------------------------------------------
        u8 *info = buffer + kInfoOffset;

        Put32(info, 0x00, kMagicInfo);
        Put32(info, 0x04, kInfoSize);       // must equal info_blck.size
        info[0x08] = kEncodingPcm16;
        info[0x09] = 0;                     // isLooped
        // 0x0A: padding u16
        Put32(info, 0x0C, sampleRate);
        Put32(info, 0x10, 0);               // loopStart
        Put32(info, 0x14, samples);         // loopEnd
        Put32(info, 0x18, 0);               // reserved

        // channelInfoRefs: { count, references[] } starting at 0x1C.
        // Reference offsets here are relative to &count, i.e. info + 0x1C.
        Put32(info, 0x1C, 1);               // one channel
        Put16(info, 0x20, kRefChannelInfo);
        Put32(info, 0x24, 0x0C);            // channelInfo sits at info+0x28

        // channelInfo { samples ref, ADPCM ref, reserved }.
        // samples.offset is relative to the DATA block's data field, which is
        // the 8 bytes after the DATA header -- so 0 puts the PCM right there.
        Put16(info, 0x28, kRefSampleData);
        Put32(info, 0x2C, 0);
        // 0x30: ADPCM reference, unused for PCM16, left zero
        // 0x38: reserved

        // ---- DATA block header -------------------------------------------
        u8 *data = buffer + kDataOffset;
        Put32(data, 0x00, kMagicData);
        Put32(data, 0x04, dataSize);
    }

}} // namespace xyoras::bcwav

#endif
