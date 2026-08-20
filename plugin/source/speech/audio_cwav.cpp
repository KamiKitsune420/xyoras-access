/*
 * XYORAS Access — audio output via CSND.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The 3DS has two audio paths. The running game owns DSP. CSND is the one
 * applets use to play over a running application without interfering, which
 * is exactly our situation -- and it is what makes speech-over-gameplay
 * possible at all. See "AI docks/06-tts-audio-pipeline.md".
 *
 * libcwav plays BCWAV. eSpeak gives us raw PCM16. So we build a minimal BCWAV
 * around the samples in memory and hand that over; nothing outside this file
 * knows the container exists.
 */
#include "xyoras/speech.hpp"

#include <3ds.h>
#include <cstring>

namespace xyoras { namespace speech {

namespace {

    /// BCWAV header fields we need to patch per utterance. The rest of the
    /// container is fixed for our case: single channel, PCM16, no loop.
    struct CwavBuild
    {
        u8 *buffer;      ///< Linear RAM: the audio hardware reads it directly.
        u32 size;
    };

    constexpr float kDefaultVolume = 1.0f;

    class CwavAudio : public IAudioOut
    {
    public:
        CwavAudio(void)
            : volume_(kDefaultVolume), ready_(false), current_(nullptr) {}

        bool Init(void) override
        {
            if (ready_)
                return true;

            // TODO(phase-1): cwavUseEnvironment(CWAV_ENV_CSND) and
            // cwavDoAptHook(). Wiring these up is roadmap task 1.3 / 1.6.
            //
            // The APT hook is not optional: without it, closing the lid or
            // opening the HOME menu leaves CSND in a bad state and speech
            // stops working for the rest of the session.

            ready_ = true;
            return ready_;
        }

        bool Play(const s16 *pcm, u32 samples, int sampleRate, float pan) override
        {
            if (!ready_ || pcm == nullptr || samples == 0)
                return false;

            CwavBuild wav;
            if (!BuildCwav(pcm, samples, sampleRate, wav))
                return false;

            // TODO(phase-1): construct a CTRPluginFramework::Sound from the
            // in-memory buffer and Play() it. The memory-buffer constructor is
            // the reason this design works -- synthesised audio never has to
            // touch the SD card.
            //
            // Handle CWAVStatus::NO_CHANNEL_AVAILABLE by dropping the
            // utterance. Never retry in a loop: the mixer being full is the
            // game's business, not ours.

            (void)pan;
            ReleaseCwav(wav);
            return true;
        }

        void Stop(void) override
        {
            // TODO(phase-1): stop the playing Sound and release its buffer.
            // Interruption has to be immediate -- a player who asks for
            // silence and waits three seconds for it has been ignored.
            current_ = nullptr;
        }

        void OnAptEvent(void) override
        {
            // TODO(phase-1): cwavNotifyAptEvent() for suspend, sleep, and exit.
        }

        ~CwavAudio(void) override { Stop(); }

    private:
        /// Wraps PCM16 mono samples in a BCWAV container in linear RAM.
        bool BuildCwav(const s16 *pcm, u32 samples, int sampleRate, CwavBuild &out)
        {
            // TODO(phase-1): emit the real header. BCWAV is a small container:
            // a header, an INFO block (encoding, sample rate, loop points,
            // channel layout) and a DATA block of samples. Format reference:
            // https://www.3dbrew.org/wiki/BCWAV
            //
            // PCM16 is deliberate. libcwav also supports IMA ADPCM on CSND,
            // which would be about four times smaller, but a few seconds of
            // speech is not worth an encode step or the quality loss.

            const u32 dataSize = samples * sizeof(s16);

            // linearAlloc, not malloc: libcwav needs the buffer where the
            // audio hardware can reach it.
            out.buffer = static_cast<u8 *>(linearAlloc(dataSize));
            out.size   = dataSize;

            if (out.buffer == nullptr)
                return false;

            std::memcpy(out.buffer, pcm, dataSize);
            (void)sampleRate;
            return true;
        }

        void ReleaseCwav(CwavBuild &wav)
        {
            if (wav.buffer != nullptr)
            {
                linearFree(wav.buffer);
                wav.buffer = nullptr;
                wav.size   = 0;
            }
        }

        float  volume_;
        bool   ready_;
        void  *current_;
    };
}

IAudioOut *CreateCwavAudio(void)
{
    return new CwavAudio();
}

}} // namespace xyoras::speech
