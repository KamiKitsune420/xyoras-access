/*
 * XYORAS Access — speech output.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * This layer SAYS things. It never reads game memory.
 * Pipeline and rationale: "AI docks/06-tts-audio-pipeline.md".
 */
#ifndef XYORAS_SPEECH_HPP
#define XYORAS_SPEECH_HPP

#include "xyoras/common.hpp"
#include <vector>

namespace xyoras { namespace speech {

    // -------------------------------------------------------------------------
    // Queue policy
    // -------------------------------------------------------------------------

    /// Lower value wins. Policy is defined in "AI docks/02-accessibility-design.md"
    /// and implemented in exactly one place: queue.cpp.
    enum class Priority
    {
        Interrupt = 0,  ///< Player asked for it. Cancels everything.
        Critical  = 1,  ///< Must be heard. Battle prompts, faint messages.
        Dialogue  = 2,  ///< Queued in order, never dropped — losing a line loses the plot.
        Ui        = 3,  ///< Replaces a pending Ui item, so fast cursor movement cannot back up.
        Ambient   = 4   ///< Dropped if anything else is pending. Step feedback.
    };

    // -------------------------------------------------------------------------
    // Backends — both swappable, neither known to the rest of the plugin
    // -------------------------------------------------------------------------

    /// Receives PCM as the synthesiser produces it, so long utterances can
    /// start playing before synthesis finishes.
    struct PcmSink
    {
        virtual bool OnSamples(const s16 *samples, u32 count) = 0;
        virtual ~PcmSink() {}
    };

    /// eSpeak NG is the current implementation; it is expected to be replaced
    /// by something less robotic, hence the interface.
    struct ISynth
    {
        virtual bool Init(void)                                       = 0;
        virtual bool Synthesize(const std::string &text, PcmSink &out) = 0;
        virtual void Cancel(void)                                     = 0;
        virtual void SetRate(int wordsPerMinute)                      = 0;
        virtual void SetPitch(int pitch)                              = 0;
        virtual int  SampleRate(void) const                           = 0;
        virtual ~ISynth() {}
    };

    /// CSND via libcwav is the only path that plays over a running game.
    struct IAudioOut
    {
        virtual bool Init(void)                                                     = 0;
        virtual bool Play(const s16 *pcm, u32 samples, int sampleRate, float pan)   = 0;
        virtual void Stop(void)                                                     = 0;
        virtual void OnAptEvent(void)                                               = 0;
        virtual ~IAudioOut() {}
    };

    /// Which audio backend the speech worker should use.
    enum class AudioBackend
    {
        Csnd,   ///< Normal playback over the running game. Real hardware.
        WavDump ///< Write .wav files to the SD card instead of playing them.
                ///< For emulators, where CSND is stubbed and nothing is heard,
                ///< and for diagnosing silence on hardware.
    };

    ISynth    *CreateEspeakSynth(void);
    IAudioOut *CreateCwavAudio(void);
    IAudioOut *CreateWavDumpAudio(void);

    // -------------------------------------------------------------------------
    // The public surface — this is what feature code uses
    // -------------------------------------------------------------------------

    /// Starts the synthesis worker thread. Returns false if speech is
    /// unavailable; the plugin stays alive either way so the menu can report it.
    bool Init(AudioBackend backend = AudioBackend::Csnd);
    void Shutdown(void);

    /// Tears down and restarts the worker on the other backend. Safe to call
    /// while running; anything queued is dropped.
    bool SetAudioBackend(AudioBackend backend);
    AudioBackend CurrentAudioBackend(void);

    /// Enqueues an utterance. Returns immediately — never blocks a game thread.
    void Say(Priority priority, const std::string &text);

    /// Silences current speech and drops everything pending.
    void StopAll(void);

    /// Re-speaks the last utterance, whatever its original priority.
    void RepeatLast(void);

    bool IsAvailable(void);

    void SetRate(int wordsPerMinute);
    void SetVolume(float volume);

    /// What the worker recorded about the most recent utterance. Used by the
    /// startup self-test to prove the pipeline ran, and to time it.
    struct SynthStats
    {
        bool ok;            ///< Synthesis completed rather than failing or being cancelled.
        u32  samples;       ///< PCM samples produced.
        u32  elapsedMs;     ///< Wall time from dequeue to audio submitted.
        int  sampleRate;    ///< Rate eSpeak reported at init.
        bool played;        ///< The audio backend accepted the buffer.

        SynthStats() : ok(false), samples(0), elapsedMs(0), sampleRate(0), played(false) {}
    };

    SynthStats LastSynthStats(void);

}} // namespace xyoras::speech

#endif
