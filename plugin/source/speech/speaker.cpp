/*
 * XYORAS Access — the synthesis worker.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Owns the synth and the audio backend, and is the only thread that touches
 * either. Game threads only ever call Say(), which enqueues and returns --
 * rule 3 in CLAUDE.md: speech must never block the game.
 */
#include "xyoras/speech.hpp"
#include "speech_internal.hpp"

#include <3ds.h>
#include <vector>

namespace xyoras { namespace speech {

namespace {

    /// Worker stack. Synthesis is not deeply recursive; eSpeak's own buffers
    /// come from the heap.
    constexpr size_t kWorkerStackSize = 32 * 1024;

    /// How long the worker sleeps when there is nothing to say. Short enough
    /// that shutdown is prompt, long enough that idling costs nothing.
    constexpr u64 kIdleWaitNs = 100ull * 1000ull * 1000ull;   // 100 ms

    Queue      g_queue;
    ISynth    *g_synth    = nullptr;
    IAudioOut *g_audio    = nullptr;
    Thread     g_worker   = nullptr;
    bool       g_running  = false;
    bool       g_available = false;

    CTRPluginFramework::Mutex g_lastMutex;
    std::string               g_lastText;

    /// Collects PCM from eSpeak and hands it to the audio backend.
    ///
    /// Currently buffers a whole utterance before playing it. Chunked
    /// streaming -- submitting the first block as soon as it exists -- is the
    /// first mitigation if Old 3DS latency measures badly (roadmap 1.5).
    class BufferSink : public PcmSink
    {
    public:
        BufferSink(void) { samples_.reserve(22050); }   // about a second

        bool OnSamples(const s16 *samples, u32 count) override
        {
            if (g_queue.TakeCancelRequest())
                return false;       // abandon this utterance

            samples_.insert(samples_.end(), samples, samples + count);
            return true;
        }

        const std::vector<s16> &Samples(void) const { return samples_; }
        bool Empty(void) const { return samples_.empty(); }

    private:
        std::vector<s16> samples_;
    };

    void WorkerMain(void *)
    {
        while (g_running)
        {
            Item item;
            if (!g_queue.Pop(item))
            {
                g_queue.WaitForWork(kIdleWaitNs);
                continue;
            }

            // A cancel that arrived while we were idle applies to the item we
            // just took, not to the next one.
            g_queue.TakeCancelRequest();

            if (g_synth == nullptr || g_audio == nullptr)
                continue;

            BufferSink sink;
            if (!g_synth->Synthesize(item.text, sink))
                continue;           // cancelled, or synthesis failed

            if (sink.Empty())
                continue;

            g_audio->Play(sink.Samples().data(),
                          static_cast<u32>(sink.Samples().size()),
                          g_synth->SampleRate(),
                          0.0f /* centred */);

            {
                CTRPluginFramework::Lock lock(g_lastMutex);
                g_lastText = item.text;
            }
        }
    }
}

bool Init(void)
{
    if (g_available)
        return true;

    g_synth = CreateEspeakSynth();
    g_audio = CreateCwavAudio();

    if (g_synth == nullptr || g_audio == nullptr)
    {
        Shutdown();
        return false;
    }

    // Either failing leaves the plugin alive but mute. The settings menu can
    // then report why, which is more useful than a silent no-op.
    if (!g_synth->Init() || !g_audio->Init())
    {
        Shutdown();
        return false;
    }

    g_running = true;
    g_worker  = threadCreate(WorkerMain, nullptr, kWorkerStackSize,
                             0x30 /* slightly below the game's threads */,
                             -1   /* any core */,
                             false);

    if (g_worker == nullptr)
    {
        g_running = false;
        Shutdown();
        return false;
    }

    g_available = true;
    return true;
}

void Shutdown(void)
{
    if (g_running)
    {
        g_running = false;
        g_queue.Clear();            // also signals the worker awake

        if (g_synth != nullptr)
            g_synth->Cancel();

        if (g_worker != nullptr)
        {
            threadJoin(g_worker, U64_MAX);
            threadFree(g_worker);
            g_worker = nullptr;
        }
    }

    delete g_synth;  g_synth = nullptr;
    delete g_audio;  g_audio = nullptr;

    g_available = false;
}

void Say(Priority priority, const std::string &text)
{
    if (!g_available)
        return;

    if (priority == Priority::Interrupt || priority == Priority::Critical)
    {
        if (g_synth != nullptr)
            g_synth->Cancel();
        if (g_audio != nullptr)
            g_audio->Stop();
    }

    g_queue.Push(priority, text);
}

void StopAll(void)
{
    if (!g_available)
        return;

    g_queue.Clear();
    g_synth->Cancel();
    g_audio->Stop();
}

void RepeatLast(void)
{
    std::string text;
    {
        CTRPluginFramework::Lock lock(g_lastMutex);
        text = g_lastText;
    }

    if (!text.empty())
        Say(Priority::Interrupt, text);
}

bool IsAvailable(void)
{
    return g_available;
}

void SetRate(int wordsPerMinute)
{
    if (g_synth != nullptr)
        g_synth->SetRate(wordsPerMinute);
}

void SetVolume(float /*volume*/)
{
    // TODO(phase-1): forward to the Sound object once playback is wired up.
}

}} // namespace xyoras::speech
