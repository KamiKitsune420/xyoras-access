# 06 — TTS and audio pipeline

The single hardest technical question in this project was: **can a 3DS plugin
speak while a game is running?** The answer is yes, and this file explains the
path.

## The pipeline

```
  Utterance (English text, priority)
        │
        ▼
  eSpeak NG  ──────────────►  PCM16 mono, 22050 Hz
  (libespeak-ng.a, ARM11)     via synth callback
        │
        ▼
  BCWAV wrapper (in memory)    minimal CWAV header around the PCM
        │
        ▼
  CTRPF Sound / libcwav
        │
        ▼
  CSND system service  ──────►  speakers, mixed over the game's audio
```

## Why eSpeak NG

- **Formant synthesis** — no recorded voice corpus, so the whole engine plus
  English data is small enough to live in a plugin's heap.
- **Fast.** Formant synthesis is cheap enough to have run on the Nintendo DS.
  ARM11 at 268 MHz (Old 3DS) should manage well above real time; New 3DS at
  804 MHz has ample headroom.
- **Portable C.** Builds with CMake and cross-compiles cleanly to bare-metal
  ARM with devkitARM.
- **Screen-reader-idiomatic.** It is the voice blind users on Linux and
  low-power devices already know, and it is intelligible at very high speech
  rates, which experienced users want.
- **Already on hand.** A working source tree with a devkitPro cross-compile
  setup exists locally (see `07-build-environment.md`).

The trade is voice quality: eSpeak is robotic. That is an accepted starting
point, explicitly marked as replaceable — the synth sits behind an interface
(`ISynth`) so a better engine can be swapped in without touching anything else.

### Build configuration for the 3DS

Mirroring the known-good Wii configuration, with the same reasoning:

| Option | Value | Why |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | `OFF` | Static only; no dynamic linking on 3DS |
| `USE_ASYNC` | `OFF` | We own threading; eSpeak's async layer wants pthreads |
| `USE_LIBPCAUDIO` | `OFF` | We supply our own audio output |
| `USE_LIBSONIC` | `OFF` | Not needed; rate control is done in eSpeak |
| `USE_MBROLA` | `OFF` | Needs external voice databases |
| `USE_KLATT` | `ON` | Alternative synthesiser, small, sometimes clearer |
| `COMPILE_INTONATIONS` | `OFF` | Avoids a host-tool build step |
| `ENABLE_TESTS` | `OFF` | Host tests cannot run on the target |

eSpeak is driven in **synth-callback** mode: `espeak_SetSynthCallback` receives
PCM as it is produced, so we can stream into our own buffer without eSpeak ever
touching an audio device.

### stdio must be made to work first

**eSpeak reads all of its voice data through stdio, and stdio does not work in
a plugin until you mount the SD yourself.** A homebrew application gets the
`sdmc:` devoptab registered by libctru before `main()`; a plugin is injected
into a running game and never goes through that path, so every `fopen` fails.

`platform::MountSdmc()` does it, and must be called before speech starts:

```c
fsInit();
archiveMountSdmc();   // NOT sdmcInit -- that is not exported by current libctru
```

Worse, eSpeak does not fail cleanly without its data: `espeak_Initialize`
**hangs**. So the four required files are checked for readability before eSpeak
is given control. Both behaviours were measured, not guessed --
`12-research-log.md`.

### Voice data

eSpeak needs its `espeak-ng-data` directory at runtime. Options:

1. **Ship it on the SD card** at `sd:/xyoras-access/espeak-ng-data/` and point
   `ESPEAK_DATA_PATH` at it. Simple, updatable, costs SD reads.
2. **Embed a trimmed English-only subset** in the plugin binary. Faster, no
   external files, but bloats the plugin and the heap.

**Decision: (1) for now.** The full data directory is a few megabytes and SD
reads happen once at init. Revisit if init latency is bad.

The English-only subset is what we actually need: `phontab`, `phonindex`,
`phondata`, `intonations`, `lang/gmw/en`, and `en_dict`. Everything else can be
dropped from the shipped copy.

## Why CSND, and why this was the blocker

A 3DS has two audio paths:

- **DSP** — what normal applications use. The running game already owns the DSP
  service. A plugin trying to use it from inside the game process risks
  fighting the game's own audio engine.
- **CSND** — the path *applets* use to play sound on top of a running or
  suspended application. It is designed for exactly this situation.

**libcwav** (with **libncsnd**) implements CWAV playback over either backend and
documents CSND as the correct choice for 3GX game plugins, explicitly because
it plays over a running application without interfering. This is the mechanism
that makes on-console speech viable at all.

CTRPF 0.8.x wraps this: its `Sound` class loads a BCWAV **from a file path or
from a memory buffer**, exposes volume, pan, and speed, and reports
`NO_CHANNEL_AVAILABLE` when the mixer is full. The memory-buffer constructor is
what lets us play synthesised audio that never touches the SD card.

Important: CSND playback must handle APT events (suspend, sleep, exit) via
`cwavDoAptHook` / `cwavNotifyAptEvent`, or audio will misbehave when the player
closes the lid or opens the HOME menu. CTRPF's `Sound` exposes a
`GOING_TO_SLEEP` status for this.

## BCWAV wrapping

BCWAV is a small container: a header, an `INFO` block describing encoding,
sample rate, loop points, and channel layout, and a `DATA` block holding
samples. libcwav supports **PCM8, PCM16, DSP ADPCM, and IMA ADPCM**; ADPCM
variants are backend-specific (DSP ADPCM plays only on DSP, IMA ADPCM only on
CSND).

Since we are on CSND and memory is not scarce for a few seconds of speech, we
use **PCM16**: no encoding step, no quality loss, trivial to emit.

Implementation: a fixed header template with a handful of fields patched
(sample count, data size, sample rate), memcpy'd in front of the PCM buffer.
`audio_cwav.cpp` owns this and nothing else knows the format exists.

The buffer must live in **linear RAM** — libcwav loads CWAV data into linear
memory for the audio hardware to reach it.

## Latency budget

Perceived responsiveness is the whole user experience. Target from event to
first audible sample:

| Stage | Budget |
| --- | --- |
| Hook/poll notices the change | < 1 frame (~17 ms) |
| Queue dispatch to worker | < 5 ms |
| eSpeak synthesis of a short phrase | 20–80 ms |
| BCWAV wrap + submit | < 5 ms |
| **Total** | **< 120 ms** |

Under ~150 ms feels immediate. If Old 3DS cannot hit this, mitigations in
order of preference:

1. **Chunked streaming** — submit the first chunk of PCM as soon as eSpeak
   produces it rather than waiting for the full utterance. eSpeak's synth
   callback makes this natural.
2. **Lower the sample rate** to 16 kHz or 11 kHz. Speech intelligibility holds
   up well and synthesis cost drops.
3. **Pre-rendered clip cache** for the highest-frequency fixed phrases
   ("blocked", "wall", direction names), rendered offline to BCWAV and loaded
   at init. Dynamic text still goes through eSpeak.

## Interruption

Stopping speech must be instant. `Sound::Stop()` on the currently playing
handle, plus clearing the queue below the interrupting priority. The worker
checks a cancellation flag between eSpeak chunks so a long utterance dies
mid-sentence rather than finishing.

## Non-speech audio

Some feedback should not be words. A movement tick, a "blocked" thud, and a
"menu wrapped" click convey state far faster than speech and do not interrupt
what the player is listening to.

These are small pre-rendered BCWAV files loaded at init and played through the
same `Sound` API. They are panned with `SetPan` where direction is meaningful —
a landmark to the player's left can be announced with a left-panned tick.

## Interfaces

```cpp
// speech/synth.hpp — swap eSpeak out without touching anything else
struct ISynth {
    virtual bool Init(const Config&) = 0;
    virtual bool Synthesize(const std::string& text, PcmSink& out) = 0;
    virtual void Cancel() = 0;
    virtual void SetRate(int wpm) = 0;
    virtual ~ISynth() {}
};

// speech/audio.hpp — swap CSND out if a better path appears
struct IAudioOut {
    virtual bool Init() = 0;
    virtual bool Play(const s16* pcm, size_t samples, int sampleRate, float pan) = 0;
    virtual void Stop() = 0;
    virtual ~IAudioOut() {}
};
```

## Known risks

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Old 3DS too slow for real-time synthesis | High | Chunked streaming, lower sample rate, clip cache |
| Plugin heap too small for eSpeak | Medium | Raise `MemorySize` in `.plgInfo`; trim voice data |
| CSND channels exhausted by the game | Medium | Handle `NO_CHANNEL_AVAILABLE`, drop the utterance |
| APT/sleep handling wrong, audio breaks after lid close | Medium | Wire `cwavDoAptHook` correctly, test lid cycles |
| eSpeak's static/global state is not thread-safe | Medium | All eSpeak calls confined to the single worker thread |
