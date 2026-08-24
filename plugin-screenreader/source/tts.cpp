#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "screenreader/tts.hpp"

#ifdef SR_HAVE_XYORAS_SPEECH
// Reuse the speech stack from xyoras-access rather than reimplementing it:
// eSpeak NG -> in-memory BCWAV -> libcwav -> CSND, with a priority queue.
// See "AI docks/06-tts-audio-pipeline.md" in that repo.
#include "xyoras/speech.hpp"
#include "xyoras/diagnostics.hpp"
#endif

namespace ScreenReader
{
    using namespace CTRPluginFramework;

    static Backend  g_backend = Backend::OSD;
    static bool     g_ready = false;

    void    Init(Backend backend)
    {
        if (g_ready)
            return;

        g_backend = backend;

#ifdef SR_HAVE_XYORAS_SPEECH
        if (backend == Backend::ESpeak)
        {
            // A marker file on the SD card switches to .wav output. Azahar
            // does not render CSND, so on the emulator this is the only way to
            // hear anything -- scripts/play-speech.sh plays the results.
            const auto backend = xyoras::diag::IsWavDumpRequested()
                                     ? xyoras::speech::AudioBackend::WavDump
                                     : xyoras::speech::AudioBackend::Csnd;
            if (!xyoras::speech::Init(backend))
            {
                OSD::Notify("screenreader: speech init failed, using OSD");
                g_backend = Backend::OSD;
            }
        }
#else
        if (backend == Backend::ESpeak)
        {
            // Built without XYORAS_ROOT pointing at the speech module.
            OSD::Notify("screenreader: built without espeak, using OSD");
            g_backend = Backend::OSD;
        }
#endif

        g_ready = true;
    }

    void    Exit(void)
    {
        if (!g_ready)
            return;

#ifdef SR_HAVE_XYORAS_SPEECH
        if (g_backend == Backend::ESpeak)
            xyoras::speech::Shutdown();
#endif
        g_ready = false;
    }

    void    Say(const std::string &text)
    {
        if (text.empty())
            return;

#ifdef SR_HAVE_XYORAS_SPEECH
        if (g_backend == Backend::ESpeak)
        {
            // Priority::Ui is the right class for menu focus: a newer item
            // replaces a pending one, so holding the d-pad cannot back up a
            // queue of stale names. The queue already de-duplicates, so the
            // hook does not need to.
            xyoras::speech::Say(xyoras::speech::Priority::Ui, text);
            return;
        }
#endif
        OSD::Notify(text);
    }

    bool    IsAvailable(void)
    {
#ifdef SR_HAVE_XYORAS_SPEECH
        if (g_backend == Backend::ESpeak)
            return xyoras::speech::IsAvailable();
#endif
        return false;   // OSD backend makes no sound
    }

    void    Flush(void)
    {
#ifdef SR_HAVE_XYORAS_SPEECH
        if (g_backend == Backend::ESpeak)
            xyoras::speech::StopAll();
#endif
    }
}
