/*
 * XYORAS Access — plugin entry points.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * An accessibility mod for Pokemon X, Y, Omega Ruby, and Alpha Sapphire.
 * Lifecycle and startup order: "AI docks/05-plugin-architecture.md".
 */
#include "xyoras/common.hpp"
#include "xyoras/game.hpp"
#include "xyoras/speech.hpp"

#include <string>

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

        void BuildSpeechEntries(PluginMenu &menu)
        {
            menu.Append(new MenuEntry("Test speech", nullptr, TestSpeech,
                "Speaks a test phrase."));

            menu.Append(new MenuEntry("Stop speech", nullptr, StopSpeech,
                "Silences current speech and clears anything pending."));

            menu.Append(new MenuEntry("Repeat last", nullptr, RepeatSpeech,
                "Repeats the last thing spoken."));
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
        PluginMenu *menu = new PluginMenu(
            "XYORAS Access", 0, 1, 0,
            "Accessibility for Pokemon Generation 6.\n"
            "Speaks dialogue, menus, battles, and the map aloud.");

        // Keeps menu updates in step with the game's frames.
        menu->SynchronizeWithFrame(true);

        // Speech comes up before the menu is populated so the status entry can
        // report honestly whether it started.
        speech::Init();

        InitMenu(*menu);

        AnnounceStartup();

        menu->Run();

        delete menu;

        speech::Shutdown();
        return 0;
    }
}
