// Speech for standalone 3DS homebrew: eSpeak NG linked in, output via NDSP.
//
// This is deliberately NOT the plugin path. A plugin injected into a game must
// use CSND (the game owns DSP) and no emulator renders CSND. A standalone app
// owns DSP, so NDSP works and is audible in Azahar.

#ifndef TTS_H
#define TTS_H

#include <3ds.h>

// Brings up NDSP and eSpeak. Returns false on failure; tts_last_error() then
// says why, in a form fit to print on screen.
//
// Call AFTER drawing something: espeak_Initialize hangs rather than failing
// when it cannot read its voice data, and a hang before the first frame is
// indistinguishable from "nothing ever loaded".
bool        tts_init(void);

void        tts_exit(void);

// Speaks text, cancelling whatever is currently playing. Safe to call on every
// cursor move: holding the d-pad must not queue up a backlog of stale names.
void        tts_say(const char *text);

// Stops playback without speaking anything new.
void        tts_stop(void);

bool        tts_ready(void);

// Human-readable reason tts_init() failed, or "" if it succeeded.
const char *tts_last_error(void);

// Samples produced by the last utterance. Diagnostic: zero here means
// synthesis failed, non-zero with no sound means the fault is downstream.
unsigned    tts_last_samples(void);

#endif
