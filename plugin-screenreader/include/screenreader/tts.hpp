#ifndef SCREENREADER_TTS_HPP
#define SCREENREADER_TTS_HPP

#include <string>

namespace ScreenReader
{
    /// Speech backend. Milestone 1 draws to the OSD so you can *see* the hook
    /// firing before any audio code exists. Milestone 2 swaps in espeak.
    enum class Backend
    {
        OSD,        ///< Draw captured text on screen. No audio. Always available.
        ESpeak      ///< Synthesise via the espeak port. Requires SR_HAVE_ESPEAK.
    };

    /// Start the worker thread. Safe to call once, from plugin main().
    void    Init(Backend backend);

    /// Stop the worker thread and release resources.
    void    Exit(void);

    /// Queue a string to be spoken. Non-blocking, safe from a hook callback.
    /// Repeated identical strings are collapsed so a redraw loop does not stutter.
    void    Say(const std::string &text);

    /// True once a synth and audio backend are up. False means Say() will
    /// fall back to the OSD rather than producing sound.
    bool    IsAvailable(void);

    /// Drop anything queued but not yet spoken (e.g. user moved the cursor again).
    void    Flush(void);
}

#endif
