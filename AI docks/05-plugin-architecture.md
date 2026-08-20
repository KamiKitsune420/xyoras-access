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
  in. It must be short, must not allocate, and must not block. Ours will only
  copy a string and push it onto a queue.

## Process model

```
                 game threads
                      │
    ┌─────────────────┴─────────────────┐
    │  Hook callbacks (game thread)     │   <- must be fast, non-blocking
    │  • message render hook            │
    │  • menu update hook               │
    └─────────────────┬─────────────────┘
                      │ push (lock-free-ish, mutex-guarded ring)
                      ▼
    ┌───────────────────────────────────┐
    │  Frame callback (CTRPF)           │   <- polls state, reads hotkeys
    │  • poll game state, diff it       │
    │  • read controller chords         │
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

Rules this enforces:

- **Nothing slow happens on a game thread.** Synthesis, file IO, and audio
  submission all live on the worker.
- **One queue, one policy.** Priority and interruption rules from
  `02-accessibility-design.md` are implemented in exactly one place.
- **The worker can be killed and restarted** without taking the game down.

## Module layout

```
plugin/
  source/
    main.cpp              Entry points, lifecycle, menu construction
    config.cpp            Settings load/save, defaults
    game/
      game_id.cpp         Title-ID + version detection, series dispatch
      addresses.cpp       The address table (the ONLY place with literals)
      memory.cpp          Guarded read helpers, pointer-chain walking
      pk6.cpp             PK6 decrypt, unshuffle, field accessors
      state.cpp           Snapshot of watched values + change detection
      hooks.cpp           Hook installation and callbacks
    speech/
      queue.cpp           Priority queue, interruption policy
      speaker.cpp         Worker thread; owns the synth + audio backends
      synth_espeak.cpp    eSpeak NG binding
      audio_cwav.cpp      PCM16 -> BCWAV -> CTRPF Sound / libcwav
      phrases.cpp         Utterance construction (the words we actually say)
    features/
      overworld.cpp       Position, facing, scans, landmarks
      battle.cpp          Battle narration
      menus.cpp           Menu narration
      readouts.cpp        Party, bag, box, Pokedex readouts
      hotkeys.cpp         Chord detection and dispatch
    data/
      names_species.cpp   Generated name tables
      names_moves.cpp
      names_items.cpp
      names_abilities.cpp
  include/xyoras/         Matching headers
```

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

## Failure policy

| Failure | Behaviour |
| --- | --- |
| Unknown title ID | Plugin should not have loaded; exit quietly |
| Untested game version | Load, speak a warning, disable memory-dependent features |
| eSpeak init fails | Log to OSD, keep menu alive, no speech |
| No audio channel available | Drop the utterance, do not retry-spin |
| Bad memory read | Return "unknown" for that field; never fault |
| Hook install fails | Disable the dependent feature, keep the rest running |
