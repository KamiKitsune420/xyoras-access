# 05 — Plugin architecture

## The delivery mechanism: 3GX

**3GX** ("3DS Game eXtension") is a plugin format for Luma3DS. It descends from
NTR CFW's `.plg` format and lets arbitrary compiled C/C++/ASM be loaded into a
game's process at launch and executed alongside it.

- Luma3DS looks for `sd:/luma/plugins/<TitleID>/<anything>.3gx`.
- A plugin named `default.3gx` in `sd:/luma/plugins/` loads for every title.
- The plugin loader ships in official Luma3DS (merged July 2023). On New 3DS it
  must be enabled from Rosalina (`L` + `Down` + `Select`).
- `3gxtool` converts a linked `.elf` plus a `.plgInfo` metadata file into a
  `.3gx`.

Because the plugin runs **inside the game process**, it can read and write game
memory directly with ordinary pointers, patch game code, and install hooks —
no IPC, no debugger attach, no address translation.

The cost is equally direct: a crash in the plugin is a crash of the game.

## The framework: CTRPluginFramework

**CTRPF** provides the runtime a plugin needs. We build against **libctrpf
0.8.x** (the `develop` line), which is the version that includes the sound
engine.

APIs we depend on:

| API | Use in this project |
| --- | --- |
| `Process::Read8/16/32/64`, `ReadString` | All game-state reads; safe, returns false on bad address |
| `Process::CheckAddress`, `CheckRegion` | Guarding pointer chains before dereference |
| `Process::GetTitleID`, `GetVersion` | Game and update-version detection at startup |
| `Hook` | Intercepting game functions (message render, menu update) |
| `Controller::IsKeyPressed/Down` | Hotkey layer |
| `OSD` | Optional on-screen debug text for sighted testers |
| `File`, `Directory` | Reading eSpeak data and the config file from SD |
| `Task`, `Thread`, `Mutex`, `Lock` | The synthesis worker thread and its queue |
| `PluginMenu`, `MenuEntry`, `MessageBox`, `Keyboard` | The settings UI |
| `Sound`, `SoundEngine` | Audio playback (see `06-tts-audio-pipeline.md`) |

### Plugin entry points

CTRPF calls four functions, which is the whole lifecycle:

```cpp
namespace CTRPluginFramework
{
    // Runs before the game starts. Safe place to patch code.
    void PatchProcess(FwkSettings &settings);

    // Runs when the process exits. Undo patches, save settings.
    void OnProcessExit(void);

    // Build the settings menu.
    void InitMenu(PluginMenu &menu);

    // Plugin main. Owns the menu and the main loop.
    int  main(void);
}
```

`main()` constructs a `PluginMenu`, registers entries, and calls `menu.Run()`,
which blocks for the life of the plugin. Our per-frame work is attached as
callbacks rather than living in `main()`.

### Hooks

`Hook` overwrites one instruction at a target address with a branch to a
callback, and can re-execute the overwritten instruction before or after the
callback. Constraints that shape our design:

- **91 enabled hooks maximum.** Plenty for us; we expect fewer than ten.
- The target instruction must not be PC-relative if we want automatic
  re-execution — position-dependent instructions must be handled manually.
- The callback runs on **the game's thread**, in whatever context the game was
  in. It must be short, must not allocate, and must not block.

**The mod does not currently install any hooks.** This section is kept because
the constraints still apply the day one is needed. Reading text — the thing
hooks were meant for — is done by scanning the heap for `nw::lyt::TextBox`
instead, which needs no code modification at all and is therefore both safer
and version-independent in a way a hook is not.

## Process model

```
                 game threads
                      │
    ┌─────────────────┴─────────────────┐
    │  Frame callback (CTRPF)           │   <- game thread: must be trivial
    │  • read controller, run the chord │
    │    state machine                  │
    │  • set a flag, return             │
    └─────────────────┬─────────────────┘
                      │ set request flag (mutex, never held over slow work)
                      ▼
    ┌───────────────────────────────────┐
    │  Narration thread                 │   <- reads the game, priority + 1
    │  • scan heap for TextBox vptrs    │      (i.e. below the game's threads)
    │  • read each pane's UTF-16 string │
    │  • decide what changed            │
    └─────────────────┬─────────────────┘
                      │ enqueue Utterance
                      ▼
    ┌───────────────────────────────────┐
    │  Speech queue (priority, mutex)   │
    └─────────────────┬─────────────────┘
                      │
                      ▼
    ┌───────────────────────────────────┐
    │  Synthesis worker thread          │   <- owns eSpeak, does the slow work
    │  • text -> PCM16 (eSpeak NG)      │
    │  • PCM16 -> in-memory BCWAV       │
    │  • Sound(buffer).Play() via CSND  │
    └───────────────────────────────────┘
```

There is **no render hook**. The message-render path was the original plan and
turned out to be unnecessary: scanning the heap for `nw::lyt::TextBox` finds the
text wherever it came from. See `12-research-log.md`.

Rules this enforces:

- **Nothing slow happens on a game thread.** Synthesis, file IO, audio
  submission and *reading game memory* all live off it. Reading counts: a full
  heap scan measured 551 ms on the game clock under emulation, and even a
  routine poll reads every tracked pane.
- **The frame callback only sets flags.** `RequestReadScreen()` and
  `RequestNewContext()` take a mutex, set a bool, and return. The narration
  thread owns everything else, so no lock is ever held across slow work.
- **One queue, one policy.** Priority and interruption rules from
  `02-accessibility-design.md` are implemented in exactly one place.
- **The worker can be killed and restarted** without taking the game down.

### Why reading has its own thread rather than sharing the speech worker

The speech worker blocks for as long as synthesis takes — hundreds of
milliseconds for a long line. Polling on that thread would mean the mod stops
watching the screen exactly while it is talking, and would miss the next line.

## Module layout

```
plugin/
  source/
    main.cpp              Entry points, lifecycle, menu construction
    platform.cpp          The things libctru does not give a plugin
    diagnostics.cpp       Checkpoints, self-test report, narration trace
    game/
      game_id.cpp         Title-ID + version detection, per-capability gate
      addresses.cpp       The address table (the ONLY place with literals)
      memory.cpp          Guarded read helpers, pointer-chain walking
    speech/
      queue.cpp           Priority queue, interruption policy
      speaker.cpp         Worker thread; owns the synth + audio backends
      synth_espeak.cpp    eSpeak NG binding
      audio_cwav.cpp      PCM16 -> BCWAV -> CTRPF Sound / libcwav
      audio_wav.cpp       .wav files on the SD card, for emulators
    features/
      narrate.cpp         The narration thread: scan, read, decide, speak
      hotkeys.cpp         Reads the controller, dispatches chords
    data/
      names_species.cpp   Generated name tables
      names_moves.cpp
      names_items.cpp
      names_abilities.cpp
  include/xyoras/         Matching headers
```

**Much of the logic lives in headers, not in `source/`.** `pk6.hpp`,
`memchain.hpp`, `vtscan.hpp`, `textbox.hpp`, `screentext.hpp`, `narration.hpp`,
`panecache.hpp`, `phrases.hpp` and `hotkeys.hpp` are header-only and free of
anything 3DS-specific, with their readers injected. That is what lets the host
tests drive the shipped code against a fake address space instead of a copy of
it — see `10-testing-and-qa.md`. The `.cpp` files under `source/` are thin: they
supply the real reader and the real threads.

Still to come, and named here so they land in the right place:
`config.cpp` (settings), `features/overworld.cpp`, `features/battle.cpp`,
`features/menus.cpp`, `features/readouts.cpp`.

There is no `hooks.cpp`. Hook installation was the plan until it turned out
that no hook is needed.

The dependency direction is strict and one-way:

```
features/  ->  game/ (read state)  and  speech/ (say things)
speech/    ->  nothing above it
game/      ->  nothing above it
```

`features/` is the only layer allowed to decide *what* to say. `game/` never
speaks; `speech/` never reads game memory.

## Startup sequence

1. `PatchProcess` — detect title ID and version. If the version is untested,
   set a flag; do not patch anything.
2. `main` — construct the menu, load config from SD.
3. Initialise the speech subsystem (eSpeak + audio). If it fails, the plugin
   stays alive so the CTRPF menu can still report the failure.
4. Speak a startup banner ("XYORAS Access ready") — this is the player's only
   confirmation that the mod loaded.
5. If the game version is untested, speak a clear warning instead of proceeding
   with unreliable offsets.
6. Install hooks, register the frame callback, run the menu loop.

## Memory budget

`.plgInfo` declares `MemorySize`, the heap reserved for the plugin. Community
plugins use around 5 MiB; we need more because eSpeak holds voice and phoneme
data plus synthesis buffers.

Planned allocation (to be measured, see `11-roadmap.md`):

| Consumer | Estimate |
| --- | --- |
| eSpeak NG runtime + loaded voice data | 2–4 MiB |
| PCM + BCWAV buffers (double-buffered) | ~1 MiB |
| Name tables (compiled in) | ~0.5 MiB |
| Queue, state, framework overhead | ~1 MiB |
| **Target `MemorySize`** | **8–12 MiB** |

Old 3DS has 128 MB total with far less free than New 3DS, so the Old-3DS
budget is the binding constraint. Measure before assuming.

**3gxtool accepts only `2MiB`, `5MiB` or `10MiB`.** Anything else is rejected
with a warning and silently replaced by 5 MiB — which is what this plugin ran
on through all emulator testing, because the field said `12MiB` and nobody read
the warning. Speech worked at 5 MiB. It is now set to `10MiB` explicitly; if
the game misbehaves on hardware, dropping back to 5 MiB is the first thing to
try, and it is known to be enough.

The narration thread adds a 32 KB stack and one 4 KB scan buffer. The buffer is
a file-scope array rather than a stack local on purpose: 4 KB is too much to
put on a 32 KB stack, and allocating it per scan inside a game process is worth
avoiding.

## What libctru does NOT give a plugin

A plugin is injected into an already-running game, so **none of libctru's
application startup runs**. Anything that startup normally sets up is simply
absent, and the APIs depending on it fail in confusing ways rather than
reporting the real cause.

Confirmed so far, both the hard way:

| Missing | Symptom | Fix |
| --- | --- | --- |
| `sdmc:` devoptab | every `fopen` fails; eSpeak **hangs** | `fsInit()` + `archiveMountSdmc()` |
| Linear heap | `linearAlloc` always returns null | `svcControlMemory(..., MEMOP_ALLOC_LINEAR, ...)` |

Both live in `platform.cpp`. Expect more of the same — romfs and the socket
service are likely candidates. **When a libctru call fails inexplicably inside
the plugin, first ask whether it depends on `__appInit`.**

## Failure policy

| Failure | Behaviour |
| --- | --- |
| Unknown title ID | Plugin should not have loaded; exit quietly |
| Untested game version | Load, speak a warning, disable memory-dependent features |
| eSpeak init fails | Log to OSD, keep menu alive, no speech |
| No audio channel available | Drop the utterance, do not retry-spin |
| Bad memory read | Return "unknown" for that field; never fault |
| Hook install fails | Disable the dependent feature, keep the rest running |
