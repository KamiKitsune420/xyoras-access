/*
 * XYORAS Access — startup self-test.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Answers "did any of this actually work?" without the player having to hear
 * anything or navigate a menu.
 *
 * Why it exists
 * -------------
 * On an emulator, CSND is stubbed (Azahar's csnd_snd.cpp logs "(STUBBED)" for
 * every call), so speech is inaudible there no matter how well it worked. And
 * on hardware, if a blind player hears nothing at startup, there is no way for
 * them to find out why. Both cases need the plugin to write down what happened.
 *
 * Enabled by creating a marker file on the SD card:
 *
 *     /xyoras-access/dump-audio
 *
 * When present, the plugin switches speech output to .wav files and writes a
 * report to /xyoras-access/diagnostics.txt. Absent, none of this runs.
 *
 * The report doubles as a test of file access itself: eSpeak reads its voice
 * data through plain fopen(), and whether newlib's sdmc: device is even mounted
 * inside a game process is not obvious. If diagnostics.txt exists at all, then
 * fopen works and that question is answered.
 */
#include "xyoras/common.hpp"
#include "xyoras/game.hpp"
#include "xyoras/speech.hpp"

#include <3ds.h>
#include <cstdio>
#include <string>

namespace xyoras { namespace diag {

namespace {
    const char *kMarkerPath = "sdmc:/xyoras-access/dump-audio";
    const char *kReportPath = "sdmc:/xyoras-access/diagnostics.txt";

    /// A test phrase with digits and punctuation, so a bad dictionary or a
    /// broken number handler shows up as a short or empty utterance.
    const char *kTestPhrase =
        "XYORAS Access self test. Route 4. Pikachu, level 25, 18 of 35 hit points.";

    std::string Hex64(u64 value)
    {
        char buf[19];
        std::snprintf(buf, sizeof(buf), "%08lX%08lX",
                      static_cast<unsigned long>(value >> 32),
                      static_cast<unsigned long>(value & 0xFFFFFFFF));
        return std::string(buf);
    }
}

bool IsSelfTestRequested(void)
{
    FILE *f = std::fopen(kMarkerPath, "rb");
    if (f == nullptr)
        return false;
    std::fclose(f);
    return true;
}

void WriteReport(void)
{
    FILE *f = std::fopen(kReportPath, "w");
    if (f == nullptr)
        return;     // no file access; nothing more we can do from in here

    std::fprintf(f, "XYORAS Access self test\n");
    std::fprintf(f, "=======================\n\n");

    std::fprintf(f, "plugin version   : %s\n", kVersion);
    std::fprintf(f, "file access      : working (this file was written)\n\n");

    std::fprintf(f, "Game\n");
    std::fprintf(f, "  title id       : %s\n",
                 Hex64(CTRPluginFramework::Process::GetTitleID()).c_str());
    std::fprintf(f, "  detected as    : %s\n", game::TitleName());
    std::fprintf(f, "  series         : %s\n",
                 game::CurrentSeries() == game::Series::XY   ? "XY"   :
                 game::CurrentSeries() == game::Series::ORAS ? "ORAS" : "unknown");
    std::fprintf(f, "  update version : %u\n", game::CurrentVersion());
    std::fprintf(f, "  offsets valid  : %s\n\n",
                 game::IsVersionSupported() ? "yes" : "no (untested version)");

    const speech::SynthStats st = speech::LastSynthStats();

    std::fprintf(f, "Speech\n");
    std::fprintf(f, "  subsystem      : %s\n",
                 speech::IsAvailable() ? "started" : "FAILED to start");
    std::fprintf(f, "  audio backend  : %s\n",
                 speech::CurrentAudioBackend() == speech::AudioBackend::WavDump
                     ? "wav files" : "CSND");
    std::fprintf(f, "  sample rate    : %d Hz\n", st.sampleRate);
    std::fprintf(f, "  synthesis      : %s\n", st.ok ? "ok" : "FAILED");
    std::fprintf(f, "  samples        : %lu\n", static_cast<unsigned long>(st.samples));

    if (st.sampleRate > 0 && st.samples > 0)
    {
        std::fprintf(f, "  audio length   : %lu ms\n",
                     static_cast<unsigned long>(st.samples * 1000ul / st.sampleRate));
    }

    std::fprintf(f, "  synth time     : %lu ms\n", static_cast<unsigned long>(st.elapsedMs));

    // The number that decides whether on-console synthesis is viable at all.
    // Above 1.0 means faster than real time, which is what we need.
    if (st.elapsedMs > 0 && st.sampleRate > 0 && st.samples > 0)
    {
        const u32 audioMs = st.samples * 1000ul / st.sampleRate;
        std::fprintf(f, "  realtime factor: %lu.%02lux\n",
                     static_cast<unsigned long>(audioMs / st.elapsedMs),
                     static_cast<unsigned long>((audioMs * 100ul / st.elapsedMs) % 100));
    }

    std::fprintf(f, "  playback       : %s\n\n", st.played ? "accepted" : "not accepted");

    std::fprintf(f, "Notes\n");
    if (!speech::IsAvailable())
    {
        std::fprintf(f, "  Speech did not start. The usual cause is missing voice data:\n");
        std::fprintf(f, "  /xyoras-access/espeak-ng-data/ must contain phondata, phontab,\n");
        std::fprintf(f, "  phonindex and en_dict.\n");
    }
    else if (!st.ok)
    {
        std::fprintf(f, "  Speech started but produced nothing. eSpeak initialised, so the\n");
        std::fprintf(f, "  voice data was found; the failure is in synthesis itself.\n");
    }
    else
    {
        std::fprintf(f, "  Pipeline works end to end. Audio written to\n");
        std::fprintf(f, "  /xyoras-access/speech/ as numbered .wav files.\n");
    }

    std::fclose(f);
}

void RunSelfTest(void)
{
    // Highest priority so nothing else can jump the queue ahead of it.
    speech::Say(speech::Priority::Interrupt, kTestPhrase);

    // The worker synthesises on its own thread; give it time to finish before
    // reading the stats. Generous, because this only runs when asked for.
    svcSleepThread(4000ull * 1000ull * 1000ull);   // 4 seconds

    WriteReport();
}

}} // namespace xyoras::diag
