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
| 1.2 | Wrap PCM16 in an in-memory BCWAV header | DONE |
| 1.3 | Play it via libcwav on CSND | DONE (verified byte-identical at CSND) |
| 1.4 | Speak a startup banner when the game boots | DONE (written to WAV; audible playback untested) |
| 1.5 | Measure end-to-end latency on New 3DS and Old 3DS | TODO (17x realtime under emulation; needs hardware) |
| 1.6 | Handle APT events (sleep, HOME suspend) correctly | DONE (ncsndInit auto-hook; untested) |
| 1.7 | Synthesis worker thread + priority queue + interruption | DONE |

**Exit criterion:** the plugin says an arbitrary sentence on demand, over game
audio, in under 150 ms, without disturbing the game.

Status: the entire pipeline is now implemented, and everything an emulator can
check has been checked — eSpeak runs on ARM11, finds its data, and synthesises correct PCM
at ~17x realtime. The audio reaching CSND is **byte-for-byte identical** to what eSpeak produced
— 71473 of 71473 samples — confirmed by patching CSND output into Azahar
(`tools/azahar-csnd-patch/`). The physical address matches from both ends
independently. Everything from text to the audio hardware's doorstep is
verified; only the DAC itself remains, which needs a console.

If 1.5 fails badly on Old 3DS, apply the mitigations in
`06-tts-audio-pipeline.md` before continuing — everything downstream depends on
speech being usable.

## Phase 2 — Reading the game

| # | Task | Status |
| --- | --- | --- |
| 2.1 | Title ID + update version detection, series dispatch | DONE |
| 2.2 | Address table with per-capability verification metadata | DONE |
| 2.3 | Guarded memory read helpers and pointer walking | DONE |
| 2.4 | Re-verify inherited community offsets on target versions | TODO |
| 2.5 | PK6 decrypt + unshuffle + field accessors | DONE (round-trip verified; not yet against real data) |
| 2.6 | Generated name tables (species, moves, items, abilities) | DONE |
| 2.7 | Find player coordinates and map ID | TODO |
| 2.8 | Read dialogue text | DONE via a different route: no render hook needed |
| 2.9 | Find menu cursor state | TODO |
| 2.10 | Wire text reading into the plugin and speak what changes | DONE (emulator only) |

**2.2 became per-capability.** A single "are the offsets verified" flag was
wrong, because the table is really two tables: the layout-text addresses this
project confirmed, and the save/battle offsets inherited from the community
that nobody has checked. One flag meant either keeping a working feature off or
switching on unverified offsets. `game::IsVerified(Capability)` asks about each
separately. `LayoutText` is verified for XY version 0; nothing else is verified
for anything.

**2.10 runs against the real game** — scan, read, choose, speak, traced against
Pokemon X in the emulator. A full heap scan measured 551 ms on the game clock,
so the cache learns where panes were last found and rescans that window in
31 ms. Both numbers are emulated and neither transfers to hardware; the ratio
does. See `12-research-log.md`.

**2.4 is now the main thing standing between here and Phase 3.** Everything in
Phase 3 that reads save-backed data — party, bag, position, map — depends on
offsets that have never been confirmed, and `IsVerified(Capability::SaveData)`
correctly refuses them. Confirming them needs a save with data in it; the one
available is a fresh game with no Pokemon anywhere.

**2.8 is solved, and not the way it was framed.** No render hook is needed at
all. Gen 6 ships RTTI, so `nw::lyt::TextBox` can be found by scanning the heap
for its vptr, and its UTF-16 string sits at `+0xD4`. Verified by reading real
text out of a running game -- "Your name?", "SAVE", and message-box content.
The same mechanism covers dialogue, menus and every other layout text, because
all of it is drawn through that one class. Details in `12-research-log.md`.

The original framing, kept because it explains the detour: the message system lives in `DllDialogCommon.cro`, a module loaded and
relocated at runtime, not at a fixed address in `code.bin`. See
`04-gen6-reverse-engineering.md`. Two approaches are already ruled out --
CRO export tables carry no named functions, and the module is not even loaded
at the title screen.

**Exit criterion:** the plugin can report, on demand, the player's coordinates,
map, party contents, and the last message the game displayed.

## Phase 3 — Core accessibility

| # | Task | Status |
| --- | --- | --- |
| 3.1 | Dialogue narration (auto-speak message boxes) | DONE (emulator only) |
| 3.2 | Repeat-last and message history | PART (repeat-last done; no history) |
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
