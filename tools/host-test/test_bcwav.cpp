/*
 * XYORAS Access — host tests for the BCWAV container builder.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * These checks mirror libcwav's own parser (cwav.c: cwav_initialize and
 * cwav_parseInfoBlock). If our bytes satisfy every condition libcwav tests,
 * cwavLoad returns CWAV_SUCCESS on hardware.
 *
 * Worth testing precisely because the failure is so opaque: a single wrong
 * field yields UNKNOWN_FILE_FORMAT or INVAID_INFO_BLOCK with no clue which,
 * and on a console the only symptom is silence.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/bcwav.hpp"

#include <cstring>
#include <vector>

using namespace xyoras;

namespace {

    u16 Get16(const u8 *p, u32 off)
    {
        u16 v;
        std::memcpy(&v, p + off, 2);
        return v;
    }

    u32 Get32(const u8 *p, u32 off)
    {
        u32 v;
        std::memcpy(&v, p + off, 4);
        return v;
    }

    std::vector<u8> BuildFile(u32 samples, u32 rate)
    {
        std::vector<u8> buf(bcwav::FileSize(samples), 0xAA);   // poison, so
        bcwav::Build(buf.data(), samples, rate);               // gaps show up
        return buf;
    }

    void TestFileHeader(void)
    {
        test::Section("file header (what cwav_initialize checks)");

        const std::vector<u8> f = BuildFile(1000, 22050);
        const u8 *p = f.data();

        // Every one of these is compared literally by libcwav, and any
        // mismatch gives CWAV_UNKNOWN_FILE_FORMAT.
        test::Equal(Get32(p, 0x00), 0x56415743u, "magic is 'CWAV'");
        test::Equal(Get16(p, 0x04), 0xFEFFu,     "endian mark");
        test::Equal(Get32(p, 0x08), 0x02010000u, "version libcwav accepts");
        test::Equal(Get16(p, 0x10), 2u,          "block count is 2");
        test::Equal(Get32(p, 0x0C), (u32)f.size(), "file size matches the buffer");
    }

    void TestBlockReferences(void)
    {
        test::Section("block references");

        const std::vector<u8> f = BuildFile(1000, 22050);
        const u8 *p = f.data();

        test::Equal(Get16(p, 0x14), 0x7000u, "info block reference type");
        test::Equal(Get16(p, 0x20), 0x7001u, "data block reference type");

        const u32 infoOff  = Get32(p, 0x18);
        const u32 infoSize = Get32(p, 0x1C);
        const u32 dataOff  = Get32(p, 0x24);
        const u32 dataSize = Get32(p, 0x28);

        test::Check(infoOff + infoSize <= f.size(), "info block lies inside the file");
        test::Check(dataOff + dataSize <= f.size(), "data block lies inside the file");
        test::Check(dataOff >= infoOff + infoSize,  "blocks do not overlap");
        test::Equal(dataOff % 0x20, 0u,             "data block is 0x20 aligned");
    }

    void TestInfoBlock(void)
    {
        test::Section("INFO block");

        const std::vector<u8> f = BuildFile(1000, 22050);
        const u8 *p = f.data();
        const u32 infoOff  = Get32(p, 0x18);
        const u32 infoSize = Get32(p, 0x1C);
        const u8 *info = p + infoOff;

        test::Equal(Get32(info, 0x00), 0x4F464E49u, "magic is 'INFO'");

        // libcwav rejects the file unless these two agree exactly.
        test::Equal(Get32(info, 0x04), infoSize, "INFO header size matches its reference");

        test::Equal(info[0x08], 1u, "encoding is PCM16");
        test::Equal(info[0x09], 0u, "not looped");
        test::Equal(Get32(info, 0x0C), 22050u, "sample rate");
        test::Equal(Get32(info, 0x14), 1000u,  "loop end is the sample count");
        test::Equal(Get32(info, 0x1C), 1u,     "one channel");
    }

    void TestChannelInfo(void)
    {
        test::Section("channel info references");

        const std::vector<u8> f = BuildFile(1000, 22050);
        const u8 *p = f.data();
        const u8 *info = p + Get32(p, 0x18);

        test::Equal(Get16(info, 0x20), 0x7100u, "channel info reference type");

        // libcwav resolves this offset relative to &channelInfoRefs.count,
        // which is info + 0x1C -- not relative to the file or the block.
        const u32 relOff  = Get32(info, 0x24);
        const u8 *chanInfo = info + 0x1C + relOff;

        test::Equal(Get16(chanInfo, 0x00), 0x1F00u, "sample data reference type");

        // And this one is relative to the DATA block's data field.
        const u32 sampleOff = Get32(chanInfo, 0x04);
        const u32 dataOff   = Get32(p, 0x24);
        const u32 pcmStart  = dataOff + 8 + sampleOff;

        test::Equal(pcmStart, bcwav::PcmOffset(), "sample offset resolves to the PCM payload");
    }

    void TestDataBlock(void)
    {
        test::Section("DATA block");

        const u32 samples = 1000;
        const std::vector<u8> f = BuildFile(samples, 22050);
        const u8 *p = f.data();
        const u8 *data = p + Get32(p, 0x24);

        test::Equal(Get32(data, 0x00), 0x41544144u, "magic is 'DATA'");
        test::Equal(Get32(data, 0x04), 8u + samples * 2u, "data size covers header plus PCM");
        test::Equal(Get32(p, 0x28), Get32(data, 0x04), "reference size agrees with the block");
    }

    void TestPayloadPlacement(void)
    {
        test::Section("PCM payload placement");

        const u32 samples = 64;
        std::vector<u8> f(bcwav::FileSize(samples), 0);
        bcwav::Build(f.data(), samples, 22050);

        // Write a recognisable payload where the caller is told to put it, and
        // confirm the container's own offsets lead back to it.
        std::vector<s16> pcm(samples);
        for (u32 i = 0; i < samples; ++i)
            pcm[i] = static_cast<s16>(i * 100);
        std::memcpy(f.data() + bcwav::PcmOffset(), pcm.data(), samples * sizeof(s16));

        const u8 *p = f.data();
        const u8 *info = p + Get32(p, 0x18);
        const u8 *chanInfo = info + 0x1C + Get32(info, 0x24);
        const u32 pcmStart = Get32(p, 0x24) + 8 + Get32(chanInfo, 0x04);

        s16 first = 0, last = 0;
        std::memcpy(&first, p + pcmStart, 2);
        std::memcpy(&last,  p + pcmStart + (samples - 1) * 2, 2);

        test::Equal(first, 0,    "first sample reachable through the references");
        test::Equal(last, 6300,  "last sample reachable through the references");
        test::Equal(bcwav::FileSize(samples), pcmStart + samples * 2u,
                    "file size leaves no slack after the PCM");
    }

    void TestSizes(void)
    {
        test::Section("size arithmetic");

        test::Equal(bcwav::FileSize(0),   bcwav::PcmOffset(), "empty payload is header only");
        test::Equal(bcwav::FileSize(1),   bcwav::PcmOffset() + 2, "one sample adds two bytes");
        test::Equal(bcwav::FileSize(100), bcwav::PcmOffset() + 200, "100 samples add 200 bytes");
    }

    void TestOtherRates(void)
    {
        test::Section("alternative sample rates");

        // The fallbacks if Old 3DS cannot synthesise fast enough at 22 kHz.
        const std::vector<u8> a = BuildFile(10, 16000);
        const std::vector<u8> b = BuildFile(10, 11025);

        test::Equal(Get32(a.data() + Get32(a.data(), 0x18), 0x0C), 16000u, "16 kHz carried through");
        test::Equal(Get32(b.data() + Get32(b.data(), 0x18), 0x0C), 11025u, "11 kHz carried through");
    }
}

int main(void)
{
    std::printf("\nBCWAV container builder\n=======================\n");

    TestFileHeader();
    TestBlockReferences();
    TestInfoBlock();
    TestChannelInfo();
    TestDataBlock();
    TestPayloadPlacement();
    TestSizes();
    TestOtherRates();

    return test::Report("bcwav");
}
