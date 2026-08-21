/*
 * XYORAS Access — host tests for the modifier-held control layer.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * These are the cases that would be miserable to debug on hardware, because
 * the only symptom is "the mod said something I did not ask for" and there is
 * no way to see which frame the buttons changed on.
 *
 * The awkward one is L+R. Two physical buttons cannot be pressed on the same
 * frame, so a chord always begins with one of them already down and the other
 * arriving as a fresh press -- which is exactly what a partner key looks like.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/hotkeys.hpp"

using namespace xyoras;
using hotkeys::Action;
using hotkeys::ChordReader;
namespace key = hotkeys::key;

namespace {

    constexpr u32 kNone = 0;
    constexpr u32 kZL   = key::kZL;
    constexpr u32 kLR   = key::kL | key::kR;

    const char *Name(Action a)
    {
        switch (a)
        {
            case Action::None:       return "None";
            case Action::ReadScreen: return "ReadScreen";
            case Action::RepeatLast: return "RepeatLast";
            case Action::StopSpeech: return "StopSpeech";
            case Action::DumpLayout: return "DumpLayout";
        }
        return "?";
    }

    void CheckAction(Action got, Action want, const char *what)
    {
        test::Check(got == want, what);
        if (got != want)
            std::printf("        wanted %s, got %s\n", Name(want), Name(got));
    }

    /// Hold a set of keys for several frames, returning anything fired.
    Action Hold(ChordReader &c, u32 keys, u32 frames)
    {
        Action fired = Action::None;
        for (u32 i = 0; i < frames; ++i)
        {
            const Action a = c.Update(keys);
            if (a != Action::None)
                fired = a;
        }
        return fired;
    }

    void TestModifierDetection(void)
    {
        test::Section("what counts as the modifier");

        test::Check(hotkeys::ModifierHeld(kZL), "ZL on its own (New 3DS)");
        test::Check(hotkeys::ModifierHeld(kLR), "L and R together (Old 3DS)");
        test::Check(!hotkeys::ModifierHeld(key::kL), "L alone is not the modifier");
        test::Check(!hotkeys::ModifierHeld(key::kR), "R alone is not the modifier");
        test::Check(!hotkeys::ModifierHeld(kNone), "nothing held is not the modifier");

        // L and R are used constantly in-game; neither may act alone.
        test::Check(!hotkeys::ModifierHeld(key::kL | key::kA), "L plus A is not the modifier");
    }

    void TestTapReadsScreen(void)
    {
        test::Section("tapping the modifier alone");

        ChordReader c;
        CheckAction(Hold(c, kZL, 5), Action::None, "nothing fires while it is held");
        CheckAction(c.Update(kNone), Action::ReadScreen, "release reads the screen");
        CheckAction(c.Update(kNone), Action::None, "and does not fire twice");
    }

    void TestTapWithLR(void)
    {
        test::Section("tapping L plus R");

        // The two buttons cannot land on the same frame. Whichever arrives
        // second must not be mistaken for a partner key.
        ChordReader c;
        c.Update(kNone);
        c.Update(key::kL);              // L first
        CheckAction(Hold(c, kLR, 4), Action::None, "quiet while both are held");
        // Letting go of L breaks the chord even though R is still down, so
        // the tap fires on that frame rather than waiting for both.
        CheckAction(c.Update(key::kR), Action::ReadScreen, "still reads the screen");
        CheckAction(c.Update(kNone), Action::None, "and not again when R follows");
    }

    void TestPartnerKeys(void)
    {
        test::Section("modifier plus a partner key");

        ChordReader c;
        Hold(c, kZL, 2);
        CheckAction(c.Update(kZL | key::kA), Action::RepeatLast, "modifier plus A repeats");
        CheckAction(c.Update(kNone), Action::None, "and the release stays silent");

        ChordReader c2;
        Hold(c2, kZL, 2);
        CheckAction(c2.Update(kZL | key::kB), Action::StopSpeech, "modifier plus B stops speech");
        CheckAction(c2.Update(kNone), Action::None, "release stays silent here too");
    }

    void TestHeldPartnerDoesNotRepeat(void)
    {
        test::Section("holding a partner key down");

        // Holding Modifier + A for a second must not queue sixty utterances.
        ChordReader c;
        Hold(c, kZL, 2);
        CheckAction(c.Update(kZL | key::kA), Action::RepeatLast, "fires once");
        CheckAction(Hold(c, kZL | key::kA, 60), Action::None, "and not again for a whole second");
    }

    void TestPartnerCanFireTwice(void)
    {
        test::Section("pressing a partner key twice in one hold");

        // Releasing and pressing A again without letting go of the modifier is
        // a deliberate second request.
        ChordReader c;
        Hold(c, kZL, 2);
        CheckAction(c.Update(kZL | key::kA), Action::RepeatLast, "first press");
        c.Update(kZL);                                  // A released
        CheckAction(c.Update(kZL | key::kA), Action::RepeatLast, "second press fires again");
    }

    void TestUnboundPartnerConsumesTheChord(void)
    {
        test::Section("modifier plus a key we have not bound yet");

        // Modifier + Y is a planned command that does not exist. It must do
        // nothing -- and crucially must NOT read the whole screen on release,
        // which is what the player would hear if the tap still counted.
        ChordReader c;
        Hold(c, kZL, 2);
        CheckAction(c.Update(kZL | key::kY), Action::None, "nothing happens");
        CheckAction(c.Update(kNone), Action::None, "and the release is silent too");
    }

    void TestLayoutDumpChord(void)
    {
        test::Section("the diagnostic snapshot chord");

        // Modifier + X asks for a layout snapshot. The chord has to work
        // repeatedly without letting go of the modifier, because the whole
        // point is to capture the same screen several times with the cursor
        // in different places.
        ChordReader c;
        Hold(c, kZL, 2);
        CheckAction(c.Update(kZL | key::kX), Action::DumpLayout, "asks for a snapshot");

        c.Update(kZL);                                  // X released
        CheckAction(c.Update(kZL | key::kX), Action::DumpLayout, "and again, without releasing");

        CheckAction(c.Update(kNone), Action::None, "the release stays silent");
    }

    void TestPlainButtonsAreIgnored(void)
    {
        test::Section("ordinary play");

        // A player mashing A through dialogue must never trigger the mod.
        ChordReader c;
        Action fired = Action::None;
        for (u32 i = 0; i < 30; ++i)
        {
            const Action a = c.Update((i % 2) ? key::kA : kNone);
            if (a != Action::None)
                fired = a;
        }
        CheckAction(fired, Action::None, "mashing A does nothing");

        // L and R are used constantly in Gen 6 -- neither alone may fire.
        fired = Action::None;
        for (u32 i = 0; i < 10; ++i)
        {
            const Action a = c.Update((i % 2) ? key::kL : kNone);
            if (a != Action::None)
                fired = a;
        }
        CheckAction(fired, Action::None, "tapping L alone does nothing");
    }

    void TestResetDropsChord(void)
    {
        test::Section("the settings menu opening mid-hold");

        // CTRPF takes the buttons while its menu is up. Coming back must not
        // fire a read-screen the player never asked for.
        ChordReader c;
        Hold(c, kZL, 3);
        c.Reset();
        CheckAction(c.Update(kNone), Action::None, "the interrupted chord is dropped");
    }

    void TestChordStartedWithKeyAlreadyDown(void)
    {
        test::Section("pressing the modifier while a key is already held");

        // Running (A held) and then reaching for ZL to hear the screen. A was
        // down before the chord began, so it is not a fresh press and must not
        // fire "repeat last" -- but the tap itself still stands, because
        // holding A is just how the player was walking, not part of a chord.
        ChordReader c;
        Hold(c, key::kA, 5);
        CheckAction(Hold(c, kZL | key::kA, 5), Action::None, "the held A does not fire");
        CheckAction(c.Update(key::kA), Action::ReadScreen,
                    "and releasing the modifier alone still reads the screen");
    }

    void TestInChord(void)
    {
        test::Section("reporting chord state");

        ChordReader c;
        test::Check(!c.InChord(), "idle to begin with");
        c.Update(kZL);
        test::Check(c.InChord(), "in a chord while held");
        c.Update(kNone);
        test::Check(!c.InChord(), "and out of it once released");
    }
}

int main(void)
{
    std::printf("\nmodifier-held control layer\n===========================\n");

    TestModifierDetection();
    TestTapReadsScreen();
    TestTapWithLR();
    TestPartnerKeys();
    TestHeldPartnerDoesNotRepeat();
    TestPartnerCanFireTwice();
    TestUnboundPartnerConsumesTheChord();
    TestLayoutDumpChord();
    TestPlainButtonsAreIgnored();
    TestResetDropsChord();
    TestChordStartedWithKeyAlreadyDown();
    TestInChord();

    return test::Report("hotkeys");
}
