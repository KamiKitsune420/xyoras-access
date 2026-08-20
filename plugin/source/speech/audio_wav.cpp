/*
 * XYORAS Access — WAV-dump audio backend (debugging).
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Writes each utterance to the SD card as a .wav file instead of playing it.
 *
 * Why this exists
 * ---------------
 * The 3DS emulators stub CSND out entirely -- Azahar's csnd_snd.cpp implements
 * the service interface and logs "(STUBBED)" for every call, so nothing is ever
 * heard. That makes the emulator useless for testing the one thing that matters
 * most, unless the audio layer can be pointed somewhere else.
 *
 * With this backend the emulator can verify everything up to playback: that the
 * plugin loads, that eSpeak initialises on ARM11, that it finds its voice data,
 * that synthesis produces sensible PCM, and how long it all takes. The
 * resulting files can be played on the host.
 *
 * It is also useful on real hardware: if speech is silent there, dumping tells
 * you immediately whether the problem is synthesis or playback.
 */
#include "xyoras/speech.hpp"
#include "xyoras/wav.hpp"

#include <3ds.h>
#include <sys/stat.h>   // mkdir
#include <cstdio>
#include <cstring>

namespace xyoras { namespace speech {

namespace {

    const char *kDumpDir = "sdmc:/xyoras-access/speech";

    class WavDumpAudio : public IAudioOut
    {
    public:
        WavDumpAudio(void) : counter_(0) {}

        bool Init(void) override
        {
            // Best effort: if the directory already exists mkdir fails and that
            // is fine. If the SD is not writable, Play() will notice.
            mkdir("sdmc:/xyoras-access", 0777);
            mkdir(kDumpDir, 0777);
            return true;
        }

        bool Play(const s16 *pcm, u32 samples, int sampleRate, float /*pan*/) override
        {
            if (pcm == nullptr || samples == 0)
                return false;

            char path[128];
            std::snprintf(path, sizeof(path), "%s/%04lu.wav",
                          kDumpDir, static_cast<unsigned long>(counter_));

            FILE *f = std::fopen(path, "wb");
            if (f == nullptr)
                return false;

            wav::Header header;
            wav::Fill(header, samples, static_cast<u32>(sampleRate));

            const bool ok =
                std::fwrite(&header, sizeof(header), 1, f) == 1 &&
                std::fwrite(pcm, sizeof(s16), samples, f) == samples;

            std::fclose(f);

            if (ok)
                ++counter_;

            return ok;
        }

        /// Nothing is playing, so there is nothing to stop. Interruption still
        /// works: the worker abandons synthesis before it ever reaches here.
        void Stop(void) override {}

        void OnAptEvent(void) override {}

    private:
        u32 counter_;
    };
}

IAudioOut *CreateWavDumpAudio(void)
{
    return new WavDumpAudio();
}

}} // namespace xyoras::speech
