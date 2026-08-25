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

    /// Worker stack.
    ///
    /// This was 32 KB, on the assumption that synthesis is not deeply recursive
    /// and that eSpeak's buffers come from the heap. That assumption is wrong:
    /// eSpeak overflowed a 32 KB stack in a standalone 3DS app running the same
    /// synthesiser, a few seconds in. The failure is not a clean abort -- the
    /// stack pointer walks below the heap base, a corrupted return address is
    /// read back off it, and the process faults at PC 0 with a NoExecuteFault.
    ///
    /// Short game strings sit under the limit, so this can hide for a long time
    /// and then surface on one long line. 512 KB matches what the standalone
    /// apps use. See "AI docks/15-home-menu-screen-reader.md".
    constexpr size_t kWorkerStackSize = 512 * 1024;

    /// How long the worker sleeps when there is nothing to say. Short enough
    /// that shutdown is prompt, long enough that idling costs nothing.
    constexpr u64 kIdleWaitNs = 100ull * 1000ull * 1000ull;   // 100 ms

    Queue      g_queue;
    ISynth    *g_synth    = nullptr;
    IAudioOut *g_audio    = nullptr;
    Thread     g_worker   = nullptr;
    bool       g_running  = false;
    bool       g_available = false;
    AudioBackend g_backend = AudioBackend::Csnd;

    CTRPluginFramework::Mutex g_lastMutex;
    std::string               g_lastText;
    SynthStats                g_stats;

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

            const u64  startMs = osGetTime();
            SynthStats stats;
            stats.sampleRate = g_synth->SampleRate();

            BufferSink sink;
            stats.ok = g_synth->Synthesize(item.text, sink);

            if (stats.ok && !sink.Empty())
            {
                stats.samples = static_cast<u32>(sink.Samples().size());
                stats.played  = g_audio->Play(sink.Samples().data(),
                                              stats.samples,
                                              g_synth->SampleRate(),
                                              0.0f /* centred */);
            }

            stats.elapsedMs = static_cast<u32>(osGetTime() - startMs);

            {
                CTRPluginFramework::Lock lock(g_lastMutex);
                g_stats = stats;
                if (stats.ok)
                    g_lastText = item.text;
            }

            if (!stats.ok || sink.Empty())
                continue;

            // Let this utterance finish before starting the next.
            //
            // Without it the queue is drained as fast as synthesis manages and
            // the results overlap -- which is why a menu was heard as all of
            // its options at once, and why a prompt was cut off by the YES/NO
            // that followed it. NVDA serialises speech absolutely for the same
            // reason, and audio-game menus queue the focused item behind the
            // menu intro rather than racing it.
            //
            // An arriving Interrupt still cuts in: the wait breaks as soon as
            // the queue has something, and the priority sort decides what wins.
            // Bounded by how long the audio actually is. Busy() depends on the
            // backend telling the truth about playback, and under emulation
            // CSND is only observed rather than played -- so it can report busy
            // forever. An unbounded wait there means the worker never returns
            // and speech stops completely after the first line, which is worse
            // than the overlap this is fixing.
            const u32 rate = (g_synth != NULL && g_synth->SampleRate() > 0)
                                 ? static_cast<u32>(g_synth->SampleRate())
                                 : 22050u;
            const u64 expectedMs = (static_cast<u64>(stats.samples) * 1000ull) / rate;
            const u64 deadline = osGetTime() + expectedMs + 500ull;

            while (g_running && g_audio != NULL && g_audio->Busy() && g_queue.Empty())
            {
                if (osGetTime() >= deadline)
                    break;
                svcSleepThread(10ull * 1000ull * 1000ull);   // 10 ms
            }
        }
    }
}

bool Init(AudioBackend backend)
{
    if (g_available)
        return true;

    g_backend = backend;
    g_synth   = CreateEspeakSynth();
    g_audio   = (backend == AudioBackend::WavDump) ? CreateWavDumpAudio()
                                                   : CreateCwavAudio();

    if (g_synth == nullptr || g_audio == nullptr)
    {
        Shutdown();
        return false;
    }

    // Either failing leaves the plugin alive but mute. The settings menu can
    // then report why, which is more useful than a silent no-op.
    diag::Checkpoint("speech: initialising synth");
    if (!g_synth->Init())
    {
        diag::Checkpoint("speech: synth Init FAILED");
        Shutdown();
        return false;
    }

    diag::Checkpoint("speech: synth ok, initialising audio");
    if (!g_audio->Init())
    {
        diag::Checkpoint("speech: audio Init FAILED");
        Shutdown();
        return false;
    }

    diag::Checkpoint("speech: audio ok, starting worker");

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

bool SetAudioBackend(AudioBackend backend)
{
    if (g_available && backend == g_backend)
        return true;

    // Restarting is simpler and safer than swapping the pointer underneath a
    // worker that may be mid-utterance.
    Shutdown();
    return Init(backend);
}

AudioBackend CurrentAudioBackend(void)
{
    return g_backend;
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

SynthStats LastSynthStats(void)
{
    CTRPluginFramework::Lock lock(g_lastMutex);
    return g_stats;
}

}} // namespace xyoras::speech
