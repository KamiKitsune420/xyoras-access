/*
 * XYORAS Access — host tests for the WAV header writer.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Small, but worth testing: a wrong size field produces a file that plays as
 * silence or noise, and on hardware that is indistinguishable from "speech is
 * broken". Getting a definite answer here removes one suspect.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/wav.hpp"

#include <cstring>
#include <vector>

using namespace xyoras;

namespace {

    std::string Tag(const char *field)
    {
        return std::string(field, 4);
    }

    void TestLayout(void)
    {
        test::Section("header layout");

        // The format fixes this at 44 bytes. If the compiler pads the struct,
        // every file written is silently malformed.
        test::Equal(sizeof(wav::Header), 44u, "header is exactly 44 bytes");
    }

    void TestTags(void)
    {
        test::Section("chunk tags");

        wav::Header h;
        wav::Fill(h, 1000, 22050);

        test::EqualStr(Tag(h.riff), "RIFF", "RIFF tag");
        test::EqualStr(Tag(h.wave), "WAVE", "WAVE tag");
        test::EqualStr(Tag(h.fmt),  "fmt ", "fmt tag (with its trailing space)");
        test::EqualStr(Tag(h.data), "data", "data tag");
    }

    void TestFields(void)
    {
        test::Section("fields for 22050 Hz mono PCM16");

        wav::Header h;
        wav::Fill(h, 1000, 22050);

        test::Equal(h.audioFormat,   1u,     "format is uncompressed PCM");
        test::Equal(h.channels,      1u,     "mono");
        test::Equal(h.bitsPerSample, 16u,    "16 bits per sample");
        test::Equal(h.sampleRate,    22050u, "sample rate carried through");
        test::Equal(h.fmtSize,       16u,    "PCM fmt chunk is 16 bytes");
        test::Equal(h.blockAlign,    2u,     "block align is channels * bytes per sample");
        test::Equal(h.byteRate,      44100u, "byte rate is sampleRate * blockAlign");
    }

    void TestSizes(void)
    {
        test::Section("size fields");

        wav::Header h;
        wav::Fill(h, 1000, 22050);

        test::Equal(h.dataSize, 2000u, "data size is samples * 2 bytes");

        // riffSize covers everything after the field itself: 36 bytes of
        // header overhead plus the samples.
        test::Equal(h.riffSize, 2036u, "riff size is 36 + data size");
    }

    void TestZeroSamples(void)
    {
        test::Section("degenerate input");

        wav::Header h;
        wav::Fill(h, 0, 22050);

        test::Equal(h.dataSize, 0u,  "zero samples gives zero data size");
        test::Equal(h.riffSize, 36u, "riff size is still the header overhead");
    }

    void TestOtherRates(void)
    {
        test::Section("alternative sample rates");

        // 16 kHz and 11 kHz are the fallbacks if Old 3DS cannot synthesise
        // fast enough at 22 kHz, so they need to be correct too.
        wav::Header h16;
        wav::Fill(h16, 100, 16000);
        test::Equal(h16.sampleRate, 16000u, "16 kHz carried through");
        test::Equal(h16.byteRate,   32000u, "16 kHz byte rate");

        wav::Header h11;
        wav::Fill(h11, 100, 11025);
        test::Equal(h11.sampleRate, 11025u, "11 kHz carried through");
        test::Equal(h11.byteRate,   22050u, "11 kHz byte rate");
    }

    void TestByteImage(void)
    {
        test::Section("on-disk byte image");

        wav::Header h;
        wav::Fill(h, 2, 22050);

        const unsigned char *raw = reinterpret_cast<const unsigned char *>(&h);

        // A player reads these at fixed offsets. Check the ones that matter,
        // little-endian, straight out of the struct as it will be written.
        test::Check(std::memcmp(raw + 0,  "RIFF", 4) == 0, "bytes 0-3 are RIFF");
        test::Check(std::memcmp(raw + 8,  "WAVE", 4) == 0, "bytes 8-11 are WAVE");
        test::Check(std::memcmp(raw + 12, "fmt ", 4) == 0, "bytes 12-15 are fmt");
        test::Check(std::memcmp(raw + 36, "data", 4) == 0, "bytes 36-39 are data");

        test::Equal(raw[20], 1u, "byte 20 is audio format low byte (PCM)");
        test::Equal(raw[22], 1u, "byte 22 is channel count low byte (mono)");
        test::Equal(raw[34], 16u, "byte 34 is bits per sample low byte");
    }
}

int main(void)
{
    std::printf("\nWAV header writer\n=================\n");

    TestLayout();
    TestTags();
    TestFields();
    TestSizes();
    TestZeroSamples();
    TestOtherRates();
    TestByteImage();

    return test::Report("wav header");
}
