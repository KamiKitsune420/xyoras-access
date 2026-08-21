# 11 — Roadmap

Status legend: `TODO` · `WIP` · `DONE` · `BLOCKED`

## Phase 0 — Foundations

| # | Task | Status |
| --- | --- | --- |
| 0.1 | Private git repository created | DONE |
| 0.2 | Research: accessibility mod techniques | DONE |
| 0.3 | Research: Gen 6 / 3DS reverse engineering | DONE |
| 0.4 | `CLAUDE.md` and `AI docks/` written | DONE |
| 0.5 | Build environment: scripts, toolchain, Makefile | DONE |
| 0.6 | Bootstrap installs libctrpf, 3gxtool, libcwav, libncsnd | DONE |
| 0.7 | eSpeak NG cross-compiles to `libespeak-ng.a` for ARM11 | DONE |
| 0.8 | `.3gx` builds, links, and packages | DONE |
| 0.9 | Host test harness (`scripts/host-test.sh`) | DONE |
| 0.10 | Plugin loads on real hardware | TODO |

**Exit criterion:** a plugin that loads in Pokémon X on a real 3DS and shows a
CTRPF menu entry.

Build output as of 2026-08-20: `libespeak-ng.a` 520 KB, `XYORASAccess.3gx`
1.2 MB, `luma.zip` 2.5 MB. Everything but hardware verification is done — that
needs a console with Luma3DS and a copy of the game. The gotchas hit along the
way are written up in `12-research-log.md`.

## Phase 1 — First words

The riskiest work, done first: prove on-console speech is viable.

| # | Task | Status |
| --- | --- | --- |
| 1.1 | Link eSpeak into the plugin; synthesise to a PCM buffer | DONE |
| 1.2 | Wrap PCM16 in an in-memory BCWAV header | TODO |
| 1.3 | Play it via CTRPF `Sound` / libcwav on CSND | TODO |
| 1.4 | Speak a startup banner when the game boots | DONE (written to WAV; audible playback untested) |
| 1.5 | Measure end-to-end latency on New 3DS and Old 3DS | TODO (17x realtime under emulation; needs hardware) |
| 1.6 | Handle APT events (sleep, HOME suspend) correctly | TODO |
| 1.7 | Synthesis worker thread + priority queue + interruption | DONE |

**Exit criterion:** the plugin says an arbitrary sentence on demand, over game
audio, in under 150 ms, without disturbing the game.

Status: everything except the audio output itself is done and verified under
emulation — eSpeak runs on ARM11, finds its data, and synthesises correct PCM
at ~17x realtime. What remains is 1.2/1.3/1.6 (BCWAV + CSND + APT handling),
which cannot be tested off-hardware because emulators stub CSND out entirely.

If 1.5 fails badly on Old 3DS, apply the mitigations in
`06-tts-audio-pipeline.md` before continuing — everything downstream depends on
speech being usable.

## Phase 2 — Reading the game

| # | Task | Status |
| --- | --- | --- |
| 2.1 | Title ID + update version detection, series dispatch | TODO |
| 2.2 | Address table with verification metadata | TODO |
| 2.3 | Guarded memory read helpers and pointer walking | TODO |
| 2.4 | Re-verify inherited community offsets on target versions | TODO |
| 2.5 | PK6 decrypt + unshuffle + field accessors | TODO |
| 2.6 | Generated name tables (species, moves, items, abilities) | TODO |
| 2.7 | Find player coordinates and map ID | TODO |
| 2.8 | Find the message-box render path and hook it | TODO |
| 2.9 | Find menu cursor state | TODO |

2.8 is the highest-value unknown in the project. See
`04-gen6-reverse-engineering.md`.

**Exit criterion:** the plugin can report, on demand, the player's coordinates,
map, party contents, and the last message the game displayed.

## Phase 3 — Core accessibility

| # | Task | Status |
| --- | --- | --- |
| 3.1 | Dialogue narration (auto-speak message boxes) | TODO |
| 3.2 | Repeat-last and message history | TODO |
| 3.3 | Position report and facing scan hotkeys | TODO |
| 3.4 | Movement feedback ticks; blocked-step cue | TODO |
| 3.5 | Map change announcements | TODO |
| 3.6 | Menu narration (focused item, position, title) | TODO |
| 3.7 | Party and bag readouts | TODO |
| 3.8 | Settings menu, config persistence to SD | TODO |

**Exit criterion:** a blind player can start a new game, get through the
opening, and walk to the first town.

## Phase 4 — Battle

| # | Task | Status |
| --- | --- | --- |
| 4.1 | Battle start/end detection and announcements | TODO |
| 4.2 | Battle message narration | TODO |
| 4.3 | Move menu narration with type, PP, category | TODO |
| 4.4 | Switch menu narration | TODO |
| 4.5 | Battle status hotkey (both active Pokémon) | TODO |
| 4.6 | HP change announcements with sensible thresholds | TODO |

**Exit criterion:** a blind player can win a gym battle unaided.

## Phase 5 — Navigation

| # | Task | Status |
| --- | --- | --- |
| 5.1 | Map collision/tile data extraction | TODO |
| 5.2 | Surroundings scan (eight tiles, run-collapsed) | TODO |
| 5.3 | Nearby landmarks with direction and distance | TODO |
| 5.4 | Directional panning for landmark cues | TODO |
| 5.5 | Pathfinding assist to a chosen landmark | TODO |

5.1 is the gate for this whole phase and may be the hardest reverse-engineering
task in the project. Fallback: infer walkability by observing whether a
movement attempt changed the player's coordinates.

## Phase 6 — Depth and polish

| # | Task | Status |
| --- | --- | --- |
| 6.1 | Box and storage readouts | TODO |
| 6.2 | Pokédex readouts | TODO |
| 6.3 | Pokémon detail screen (stats, IVs, moves, ability) | TODO |
| 6.4 | Touch-screen substitutes: PSS, Super Training, DexNav, AreaNav | TODO |
| 6.5 | Verbosity tuning from blind-user feedback | TODO |
| 6.6 | Non-speech audio cue set | TODO |
| 6.7 | User documentation in `docs/` | TODO |
| 6.8 | Public release and distribution | TODO |

## Deferred / future

- **Better voice than eSpeak.** The `ISynth` interface exists for this. A
  small neural or concatenative voice may fit on New 3DS.
- **Host-bridge output backend.** Stream utterances over Wi-Fi to a PC screen
  reader, for players who prefer their own voice.
- **Languages other than English.** `phrases.cpp` is the translation surface.
- **Generation 7 (Sun/Moon/USUM).** Same architecture, different offsets.
- **Generation 4/5 on DS.** Different platform entirely; would need its own
  delivery mechanism.

## Known blockers and open questions

| Question | Blocks | Notes |
| --- | --- | --- |
| Can Old 3DS synthesise fast enough? | Phase 1 exit | Measure at 1.5 |
| Where is the message render function? | 3.1, most of Phase 4 | Highest-value unknown |
| Is map collision data reachable at runtime? | Phase 5 | Fallback exists |
| How much heap does eSpeak actually need? | `MemorySize` tuning | Measure at 1.1 |
| Do the inherited offsets hold on current versions? | Phase 2 | Re-verify all |
