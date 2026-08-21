/*
 * XYORAS Access — startup self-test.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Answers "did any of this actually work?" without the player having to hear
 * anything or navigate a menu.
 *
 * Enabled by creating a marker file on the SD card:
 *
 *     /xyoras-access/dump-audio
 *
 * When present, speech output switches to .wav files and a report is written
 * to /xyoras-access/diagnostics.txt.
 *
 * TWO FILE APIS, DELIBERATELY
 * ---------------------------
 * Everything here uses CTRPluginFramework::File, which goes through the game
 * process's own FS session. It does NOT use fopen, and that is the point.
 *
 * eSpeak reads its voice data with plain fopen, which needs newlib's `sdmc:`
 * device to be mounted. In a homebrew application libctru sets that up during
 * startup; a plugin injected into a game process does not go through that
 * path, so whether fopen works at all here is an open question -- and if it
 * does not, eSpeak can never load its data and the mod is permanently mute.
 *
 * Every community plugin uses CTRPF's File API exclusively, which is weak
 * evidence that fopen does not work. So the report below tests fopen
 * explicitly and writes the answer down. If the report exists but says fopen
 * failed, the speech design needs rethinking and we have found that out
 * without burning a hardware session on it.
 */
#include "xyoras/common.hpp"
#include "xyoras/game.hpp"
#include "xyoras/speech.hpp"

#include <3ds.h>
#include <3ds/archive.h>
#include <cstdio>
#include <string>

namespace xyoras { namespace diag {

using CTRPluginFramework::File;

namespace {
    // Two independent markers. "self-test" asks for the report; "dump-audio"
    // additionally diverts speech to .wav files. Keeping them separate means
    // the self-test can exercise the real CSND path rather than only the
    // debug one -- which is the only way to see what CSND is actually sent.
    const char *kSelfTestPath   = "/xyoras-access/self-test";
    const char *kMarkerPath     = "/xyoras-access/dump-audio";
    const char *kReportPath     = "/xyoras-access/diagnostics.txt";
    const char *kCheckpointPath = "/xyoras-access/checkpoints.txt";
    const char *kFopenTestPath  = "sdmc:/xyoras-access/fopen-test.txt";

    /// A test phrase with digits and punctuation, so a broken dictionary or
    /// number handler shows up as a short or empty utterance.
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

    void Append(const char *path, const std::string &line)
    {
        File f;
        if (File::Open(f, path, File::RWC | File::APPEND) != 0)
            return;
        f.WriteLine(line);
        f.Close();
    }

    /// Does plain fopen work inside a game process? This is the question that
    /// decides whether eSpeak can load its voice data at all.
    bool TestFopen(void)
    {
        FILE *f = std::fopen(kFopenTestPath, "w");
        if (f == nullptr)
            return false;
        std::fputs("ok\n", f);
        std::fclose(f);
        return true;
    }
}

/// Runs the fopen probe and records the answer straight away, before anything
/// that depends on fopen has a chance to hang or crash. eSpeak loads its voice
/// data this way, so if the plugin dies during speech init, this line is what
/// tells us why.
void ProbeFopen(const char *when)
{
    Checkpoint(TestFopen() ? (std::string("fopen WORKS ") + when).c_str()
                           : (std::string("fopen FAILED ") + when).c_str());
}

/// Records how far startup got. Written unconditionally and through CTRPF's
/// File API, so it survives even when everything else fails -- if the plugin
/// runs at all, this file appears.
void Checkpoint(const char *stage)
{
    Append(kCheckpointPath, std::string("reached: ") + stage);
}

bool IsWavDumpRequested(void)
{
    return File::Exists(kMarkerPath) == 1;
}

bool IsSelfTestRequested(void)
{
    // Must not use fopen: if fopen is the thing that is broken, checking with
    // it would silently disable the very test meant to detect that.
    //
    // Exists() returns 1 for "file exists", NOT 0 for success -- unlike every
    // other call in this API, which uses 0 for success. Getting that backwards
    // silently disabled the self-test once already.
    return File::Exists(kSelfTestPath) == 1 || File::Exists(kMarkerPath) == 1;
}

void WriteReport(void)
{
    const bool fopenWorks = TestFopen();

    File f;
    if (File::Open(f, kReportPath, File::RWC | File::TRUNCATE) != 0)
        return;

    std::string r;
    r += "XYORAS Access self test\n";
    r += "=======================\n\n";
    r += "plugin version   : "; r += kVersion; r += "\n\n";

    r += "File access\n";
    r += "  CTRPF File     : working (this file was written)\n";
    r += "  plain fopen    : ";
    r += fopenWorks ? "WORKING\n" : "FAILED -- eSpeak cannot load voice data this way\n";
    r += "\n";

    r += "Game\n";
    r += "  title id       : " + Hex64(CTRPluginFramework::Process::GetTitleID()) + "\n";
    r += "  detected as    : "; r += game::TitleName(); r += "\n";
    r += "  series         : ";
    r += (game::CurrentSeries() == game::Series::XY)   ? "XY\n"
       : (game::CurrentSeries() == game::Series::ORAS) ? "ORAS\n" : "unknown\n";
    r += "  update version : " + std::to_string(game::CurrentVersion()) + "\n";
    r += "  offsets valid  : ";
    r += game::IsVersionSupported() ? "yes\n" : "no (untested version)\n";
    r += "\n";

    const speech::SynthStats st = speech::LastSynthStats();

    r += "Speech\n";
    r += "  subsystem      : ";
    r += speech::IsAvailable() ? "started\n" : "FAILED to start\n";
    r += "  audio backend  : ";
    r += (speech::CurrentAudioBackend() == speech::AudioBackend::WavDump)
       ? "wav files\n" : "CSND\n";
    r += "  sample rate    : " + std::to_string(st.sampleRate) + " Hz\n";
    r += "  synthesis      : ";
    r += st.ok ? "ok\n" : "FAILED\n";
    r += "  samples        : " + std::to_string(st.samples) + "\n";

    if (st.sampleRate > 0 && st.samples > 0)
        r += "  audio length   : " + std::to_string(st.samples * 1000u / st.sampleRate) + " ms\n";

    r += "  synth time     : " + std::to_string(st.elapsedMs) + " ms\n";

    // The number that decides whether on-console synthesis is viable at all.
    // Above 1.0 means faster than real time, which is what we need.
    if (st.elapsedMs > 0 && st.sampleRate > 0 && st.samples > 0)
    {
        const u32 audioMs = st.samples * 1000u / st.sampleRate;
        r += "  realtime factor: " + std::to_string(audioMs / st.elapsedMs) + "."
           + std::to_string((audioMs * 100u / st.elapsedMs) % 100) + "x\n";
    }

    r += "  playback       : ";
    r += st.played ? "accepted\n" : "not accepted\n";
    r += "\n";

    r += "Notes\n";
    if (!fopenWorks)
    {
        r += "  fopen does not work in this process, so eSpeak cannot read its\n";
        r += "  voice data. Speech will never start until the data is supplied\n";
        r += "  another way. This is a design-level finding, not a bad install.\n";
    }
    else if (!speech::IsAvailable())
    {
        r += "  Speech did not start even though file access works. Check that\n";
        r += "  /xyoras-access/espeak-ng-data/ contains phondata, phontab,\n";
        r += "  phonindex and en_dict.\n";
    }
    else if (!st.ok)
    {
        r += "  eSpeak initialised, so it found its voice data, but synthesis\n";
        r += "  produced nothing. The failure is in synthesis itself.\n";
    }
    else
    {
        r += "  Pipeline works end to end. Audio written to\n";
        r += "  /xyoras-access/speech/ as numbered .wav files.\n";
    }

    f.Write(r.c_str(), static_cast<u32>(r.size()));
    f.Close();
}

void RunSelfTest(void)
{
    Checkpoint("self test started");

    // Highest priority so nothing can jump the queue ahead of it.
    speech::Say(speech::Priority::Interrupt, kTestPhrase);

    // The worker synthesises on its own thread; give it time to finish before
    // reading the stats. Generous, because this only runs when asked for.
    svcSleepThread(4000ull * 1000ull * 1000ull);   // 4 seconds

    Checkpoint("writing report");
    WriteReport();
    Checkpoint("self test complete");
}

}} // namespace xyoras::diag
