/*
 * XYORAS Access — audio output via CSND.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * The 3DS has two audio paths. The running game owns DSP. CSND is the one
 * applets use to play over a running application without interfering, which is
 * exactly our situation -- and it is what makes speech-over-gameplay possible
 * at all. See "AI docks/06-tts-audio-pipeline.md".
 *
 * libcwav plays BCWAV; eSpeak gives us raw PCM16. So we wrap the samples in a
 * BCWAV container in linear memory and hand that over. Nothing outside this
 * file knows the container exists.
 */
#include "xyoras/speech.hpp"
#include "xyoras/bcwav.hpp"
#include "xyoras/platform.hpp"

#include <3ds.h>
#include <csvc.h>
#include <cwav.h>
#include <ncsnd.h>

#include <cstdio>
#include <cstring>

namespace xyoras { namespace speech {

namespace {

    /// How many utterances can be in flight before the oldest is reclaimed.
    /// Speech is mostly sequential, so this only has to cover an interruption
    /// overlapping the tail of whatever it interrupted.
    constexpr int kSlots = 4;

    /// Virtual-to-physical translation for libcwav.
    ///
    /// libcwav defaults to osConvertVirtToPhys, which relies on libctru
    /// knowing the process memory map -- knowledge a plugin injected into
    /// someone else's process does not have. Luma exposes svcConvertVAToPA for
    /// exactly this case and CTRPF links it. Getting this wrong hands CSND a
    /// garbage address, which plays as noise or silence.
    u32 PluginVaToPa(const void *addr)
    {
        return svcConvertVAToPA(addr, false);
    }

    /// One in-flight utterance: its BCWAV buffer plus libcwav's handle to it.
    struct Slot
    {
        u8  *buffer;
        u32  size;
        CWAV cwav;
        bool loaded;

        Slot() : buffer(nullptr), size(0), loaded(false)
        {
            std::memset(&cwav, 0, sizeof(cwav));
        }
    };

    class CwavAudio : public IAudioOut
    {
    public:
        CwavAudio(void) : ready_(false), next_(0), volume_(1.0f) {}

        bool Init(void) override
        {
            if (ready_)
                return true;

            // true: let libncsnd install its own APT hook. Without APT
            // handling, closing the lid or opening the HOME menu leaves CSND
            // in a bad state and speech stops for the rest of the session.
            if (R_FAILED(ncsndInit(true)))
                return false;

            cwavUseEnvironment(CWAV_ENV_CSND);
            cwavSetVAToPACallback(PluginVaToPa);

            ready_ = true;
            return true;
        }

        bool Play(const s16 *pcm, u32 samples, int sampleRate, float pan) override
        {
            if (!ready_ || pcm == nullptr || samples == 0)
                return false;

            Slot &slot = slots_[next_];

            // Reclaim the slot unless it is still audible.
            if (slot.loaded)
            {
                if (cwavIsPlaying(&slot.cwav))
                    return false;   // every slot busy; drop rather than cut speech short
                Release(slot);
            }

            next_ = (next_ + 1) % kSlots;

            const u32 size = bcwav::FileSize(samples);

            // Must be physically contiguous: CSND reads it by physical
            // address. platform::LinearAlloc goes to the kernel, because
            // libctru's linearAlloc has no heap to draw from inside a plugin.
            slot.buffer = static_cast<u8 *>(platform::LinearAlloc(size));
            if (slot.buffer == nullptr)
            {
                diag::Checkpoint("cwav: LinearAlloc FAILED");
                return false;
            }
            slot.size = size;

            bcwav::Build(slot.buffer, samples, static_cast<u32>(sampleRate));
            std::memcpy(slot.buffer + bcwav::PcmOffset(), pcm, samples * sizeof(s16));

            // The audio hardware reads physical memory and will not see
            // anything still sitting in our data cache.
            GSPGPU_FlushDataCache(slot.buffer, size);

            cwavLoad(&slot.cwav, slot.buffer, 1);
            if (slot.cwav.loadStatus != CWAV_SUCCESS)
            {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "cwav: load FAILED status=%d",
                              (int)slot.cwav.loadStatus);
                diag::Checkpoint(msg);
                platform::LinearFree(slot.buffer, slot.size);
                slot.buffer = nullptr;
                slot.size   = 0;
                return false;
            }

            slot.loaded       = true;
            slot.cwav.volume  = volume_;
            slot.cwav.monoPan = pan;

            // -1 as the right channel plays the single channel as mono.
            const cwavPlayResult result = cwavPlay(&slot.cwav, 0, -1);

            if (result.playStatus != CWAV_SUCCESS)
            {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "cwav: play FAILED status=%d",
                              (int)result.playStatus);
                diag::Checkpoint(msg);
                // NO_CHANNEL_AVAILABLE means the mixer is full. That is the
                // game's business, not ours: drop the utterance rather than
                // spin waiting for a channel to free up.
                Release(slot);
                return false;
            }

            diag::Checkpoint("cwav: play OK");
            return true;
        }

        void Stop(void) override
        {
            if (!ready_)
                return;

            // Interruption has to be immediate: a player who asks for silence
            // and waits three seconds for it has been ignored.
            for (int i = 0; i < kSlots; ++i)
            {
                if (slots_[i].loaded)
                {
                    cwavStop(&slots_[i].cwav, 0, -1);
                    Release(slots_[i]);
                }
            }
        }

        void OnAptEvent(void) override
        {
            // ncsndInit(true) installed the hook, so this is only needed if
            // that is ever changed to false.
        }

        ~CwavAudio(void) override
        {
            Stop();
            if (ready_)
                ncsndExit();
        }

    private:
        void Release(Slot &slot)
        {
            if (slot.loaded)
            {
                cwavFree(&slot.cwav);
                slot.loaded = false;
            }
            if (slot.buffer != nullptr)
            {
                platform::LinearFree(slot.buffer, slot.size);
                slot.buffer = nullptr;
                slot.size   = 0;
            }
        }

        bool  ready_;
        Slot  slots_[kSlots];
        int   next_;
        float volume_;
    };
}

IAudioOut *CreateCwavAudio(void)
{
    return new CwavAudio();
}

}} // namespace xyoras::speech
