/*
 * XYORAS Access — eSpeak NG synthesis backend.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * eSpeak NG is GPLv3 and is linked into this plugin, which is why the whole
 * project is GPLv3. See "AI docks/14-legal-and-licensing.md".
 *
 * THREADING: eSpeak keeps global state and is not thread-safe. Every call in
 * this file happens on the synthesis worker thread and nowhere else.
 */
#include "xyoras/speech.hpp"
#include "xyoras/platform.hpp"

#include <espeak-ng/speak_lib.h>
#include <cstdio>
#include <cstring>

namespace xyoras { namespace speech {

namespace {

    /// Where the trimmed English voice data is staged on the SD card.
    /// Written by scripts/build-espeak-3ds.sh, shipped by scripts/package.sh.
    const char *kDataPath = "sdmc:/xyoras-access";

    /// Screen-reader users routinely run far faster than this; it is only the
    /// default. eSpeak accepts roughly 80-450 wpm.
    constexpr int kDefaultRateWpm = 200;
    constexpr int kDefaultPitch   = 50;

    /// The sink for the utterance currently being synthesised. eSpeak's
    /// callback is a plain C function pointer with no user-data parameter, so
    /// this has to be a file-scope pointer. Safe because synthesis is confined
    /// to the single worker thread.
    PcmSink *g_activeSink = nullptr;
    bool     g_cancelled  = false;

    /// eSpeak calls this as samples are produced, which is what lets us start
    /// playback before the whole utterance is synthesised.
    /// Returning non-zero asks eSpeak to abort the current utterance.
    int SynthCallback(short *wav, int numsamples, espeak_EVENT * /*events*/)
    {
        if (g_cancelled)
            return 1;

        // wav == nullptr signals end of synthesis.
        if (wav == nullptr || numsamples <= 0)
            return 0;

        if (g_activeSink == nullptr)
            return 1;

        if (!g_activeSink->OnSamples(reinterpret_cast<const s16 *>(wav),
                                     static_cast<u32>(numsamples)))
            return 1;

        return 0;
    }

    /// The four files eSpeak cannot start without. Checking them ourselves
    /// turns a hang into a clean, reportable failure.
    bool VoiceDataPresent(void)
    {
        static const char *kRequired[] = {
            "sdmc:/xyoras-access/espeak-ng-data/phondata",
            "sdmc:/xyoras-access/espeak-ng-data/phontab",
            "sdmc:/xyoras-access/espeak-ng-data/phonindex",
            "sdmc:/xyoras-access/espeak-ng-data/en_dict",
        };

        for (size_t i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); ++i)
        {
            FILE *f = std::fopen(kRequired[i], "rb");
            if (f == nullptr)
                return false;
            std::fclose(f);
        }
        return true;
    }

    class EspeakSynth : public ISynth
    {
    public:
        EspeakSynth(void) : sampleRate_(0), ready_(false) {}

        bool Init(void) override
        {
            if (ready_)
                return true;

            // eSpeak does NOT fail cleanly when it cannot read its voice data:
            // espeak_Initialize hangs, and since this runs on the plugin's own
            // thread it takes the mod down silently. A blind player would get
            // no banner and no way to find out why, which is the exact failure
            // mode rule 5 in CLAUDE.md exists to prevent.
            //
            // So check the data is reachable BEFORE handing control to eSpeak.
            if (!platform::IsSdmcMounted())
            {
                diag::Checkpoint("espeak: SD not mounted, refusing to initialise");
                return false;
            }

            if (!VoiceDataPresent())
            {
                diag::Checkpoint("espeak: voice data missing, refusing to initialise");
                return false;
            }

            diag::Checkpoint("espeak: calling espeak_Initialize");

            // AUDIO_OUTPUT_SYNCHRONOUS: eSpeak never touches an audio device.
            // It hands us PCM through the callback and we own playback.
            const int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS,
                                               0 /* no internal buffer */,
                                               kDataPath,
                                               0 /* options */);
            if (rate <= 0)
            {
                diag::Checkpoint("espeak: espeak_Initialize FAILED (no voice data?)");
                return false;
            }

            diag::Checkpoint("espeak: initialised, selecting voice");

            sampleRate_ = rate;
            espeak_SetSynthCallback(&SynthCallback);

            if (espeak_SetVoiceByName("en") != EE_OK)
            {
                // Fall back to whatever voice is present rather than going mute.
                if (espeak_SetVoiceByName("en-us") != EE_OK)
                    return false;
            }

            SetRate(kDefaultRateWpm);
            SetPitch(kDefaultPitch);

            diag::Checkpoint("espeak: ready");
            ready_ = true;
            return true;
        }

        bool Synthesize(const std::string &text, PcmSink &out) override
        {
            if (!ready_ || text.empty())
                return false;

            g_activeSink = &out;
            g_cancelled  = false;

            const espeak_ERROR err = espeak_Synth(
                text.c_str(),
                text.size() + 1,
                0,                      // start position
                POS_CHARACTER,
                0,                      // end position: 0 means to the end
                espeakCHARS_UTF8,
                nullptr,                // no unique identifier needed
                nullptr);               // no user data

            g_activeSink = nullptr;

            return err == EE_OK && !g_cancelled;
        }

        void Cancel(void) override
        {
            // Set the flag first: the callback may be running right now, and
            // it is what actually stops synthesis mid-utterance.
            g_cancelled = true;
            if (ready_)
                espeak_Cancel();
        }

        void SetRate(int wordsPerMinute) override
        {
            if (wordsPerMinute < 80)  wordsPerMinute = 80;
            if (wordsPerMinute > 450) wordsPerMinute = 450;
            espeak_SetParameter(espeakRATE, wordsPerMinute, 0);
        }

        void SetPitch(int pitch) override
        {
            if (pitch < 0)   pitch = 0;
            if (pitch > 100) pitch = 100;
            espeak_SetParameter(espeakPITCH, pitch, 0);
        }

        int SampleRate(void) const override { return sampleRate_; }

        ~EspeakSynth(void) override
        {
            if (ready_)
                espeak_Terminate();
        }

    private:
        int  sampleRate_;
        bool ready_;
    };
}

ISynth *CreateEspeakSynth(void)
{
    return new EspeakSynth();
}

}} // namespace xyoras::speech
