/*
 * XYORAS Access — plugin entry points.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * An accessibility mod for Pokemon X, Y, Omega Ruby, and Alpha Sapphire.
 * Lifecycle and startup order: "AI docks/05-plugin-architecture.md".
 */
#include "xyoras/common.hpp"
#include "xyoras/game.hpp"
#include "xyoras/platform.hpp"
#include "xyoras/speech.hpp"

#include <string>

namespace xyoras { namespace diag {
    bool IsSelfTestRequested(void);
    bool IsWavDumpRequested(void);
    void RunSelfTest(void);
    void Checkpoint(const char *stage);
    void ProbeFopen(const char *when);
}}

namespace CTRPluginFramework
{
    using namespace xyoras;

    namespace
    {
        /// Speaks the state of the world at startup. This banner is the
        /// player's only confirmation that the mod loaded at all -- without
        /// sight there is nothing else to check.
        void AnnounceStartup(void)
        {
            if (!speech::IsAvailable())
                return;

            std::string banner = "XYORAS Access ";
            banner += kVersion;
            banner += " ready. ";
            banner += game::TitleName();
            banner += ".";

            speech::Say(speech::Priority::Critical, banner);

            // Better to say plainly that readings cannot be trusted than to
            // speak confident nonsense derived from the wrong offsets.
            if (!game::IsVersionSupported())
            {
                speech::Say(speech::Priority::Critical,
                            "Warning. This game update version has not been tested. "
                            "Features that read game data are disabled.");
            }
        }

        /// MenuEntry callbacks are plain function pointers, not std::function,
        /// so they cannot capture. Anything a callback needs lives out here.
        std::string g_statusText;

        void ShowStatus(MenuEntry *)
        {
            MessageBox("Status", g_statusText)();
        }

        void BuildStatusEntry(PluginMenu &menu)
        {
            g_statusText  = "XYORAS Access ";
            g_statusText += kVersion;
            g_statusText += "\n\nGame: ";
            g_statusText += game::TitleName();
            g_statusText += "\nUpdate version: ";
            g_statusText += std::to_string(game::CurrentVersion());
            g_statusText += "\nOffsets verified: ";
            g_statusText += game::IsVersionSupported() ? "yes" : "no";
            g_statusText += "\nSpeech: ";
            g_statusText += speech::IsAvailable() ? "ready" : "unavailable";
            g_statusText += "\nAudio output: ";
            g_statusText += (speech::CurrentAudioBackend() == speech::AudioBackend::WavDump)
                          ? "WAV files on SD" : "CSND";

            menu.Append(new MenuEntry("Status", nullptr, ShowStatus,
                "Shows the detected game, update version, and whether speech started."));
        }

        void TestSpeech(MenuEntry *)
        {
            speech::Say(speech::Priority::Interrupt,
                        "XYORAS Access speech test. "
                        "If you can hear this, the audio pipeline is working.");
        }

        void StopSpeech(MenuEntry *)   { speech::StopAll();    }
        void RepeatSpeech(MenuEntry *) { speech::RepeatLast(); }

        /// Emulators stub CSND out, so nothing is ever heard there. Switching
        /// to the WAV backend makes the emulator useful anyway: everything up
        /// to playback still runs, and the result lands on the SD card where
        /// it can be listened to on a computer.
        void ToggleAudioBackend(MenuEntry *)
        {
            const bool toWav =
                speech::CurrentAudioBackend() == speech::AudioBackend::Csnd;

            speech::SetAudioBackend(toWav ? speech::AudioBackend::WavDump
                                          : speech::AudioBackend::Csnd);

            MessageBox("Audio output",
                       toWav ? "Now writing speech to\n"
                               "sdmc:/xyoras-access/speech/\n\n"
                               "Nothing will be heard. Play the files on a computer."
                             : "Now playing speech through CSND.")();
        }

        void BuildSpeechEntries(PluginMenu &menu)
        {
            menu.Append(new MenuEntry("Test speech", nullptr, TestSpeech,
                "Speaks a test phrase."));

            menu.Append(new MenuEntry("Stop speech", nullptr, StopSpeech,
                "Silences current speech and clears anything pending."));

            menu.Append(new MenuEntry("Repeat last", nullptr, RepeatSpeech,
                "Repeats the last thing spoken."));

            menu.Append(new MenuEntry("Toggle audio output", nullptr, ToggleAudioBackend,
                "Switch between playing speech and writing it to the SD card as "
                ".wav files. Use the file output on emulators, where CSND is not "
                "emulated and nothing can be heard."));
        }
    }

    /// Runs before the game starts. The safe place to patch code.
    void PatchProcess(FwkSettings &settings)
    {
        // Identify first: everything downstream, including whether it is safe
        // to touch game memory at all, depends on knowing which build we are in.
        game::Identify();

        (void)settings;
    }

    /// Runs when the process exits. Undo patches, release resources.
    void OnProcessExit(void)
    {
        speech::Shutdown();
    }

    void InitMenu(PluginMenu &menu)
    {
        BuildStatusEntry(menu);
        BuildSpeechEntries(menu);
    }

    int main(void)
    {
        // Checkpoints go through CTRPF's File API and are written before
        // anything else can fail. If this file appears at all, the plugin ran;
        // how far the list gets says where it stopped. Without this there is no
        // way to tell "CTRPF never started" from "CTRPF ran but could not write
        // files", and those call for completely different fixes.
        diag::Checkpoint("plugin main entered");

        // MUST come before speech starts. stdio does not work in a game
        // process until this runs, and eSpeak reads all its voice data through
        // stdio -- without it, espeak_Initialize hangs rather than failing.
        // See platform.cpp.
        const bool sdmcOk = platform::MountSdmc();
        diag::Checkpoint(sdmcOk ? "sdmc mounted" : "sdmc mount FAILED");

        PluginMenu *menu = new PluginMenu(
            "XYORAS Access", 0, 1, 0,
            "Accessibility for Pokemon Generation 6.\n"
            "Speaks dialogue, menus, battles, and the map aloud.");

        // Keeps menu updates in step with the game's frames.
        menu->SynchronizeWithFrame(true);

        // A marker file on the SD card switches speech to .wav output and asks
        // for a written report. This is the only way to see what happened on an
        // emulator, where CSND is stubbed and nothing can be heard.
        const bool selfTest = diag::IsSelfTestRequested();
        diag::Checkpoint(selfTest ? "self test requested" : "no self test marker");

        // Speech comes up before the menu is populated so the status entry can
        // report honestly whether it started.
        speech::Init(diag::IsWavDumpRequested() ? speech::AudioBackend::WavDump
                                                : speech::AudioBackend::Csnd);

        diag::Checkpoint(speech::IsAvailable() ? "speech started"
                                              : "speech FAILED to start");

        InitMenu(*menu);

        AnnounceStartup();

        if (selfTest)
            diag::RunSelfTest();

        diag::Checkpoint("entering menu loop");

        menu->Run();

        delete menu;

        speech::Shutdown();
        return 0;
    }
}
