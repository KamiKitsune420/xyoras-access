# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this project is

**XYORAS Access** is a blindness/low-vision accessibility mod for the four
Generation 6 Pokémon games on Nintendo 3DS:

| Game | Title ID |
| --- | --- |
| Pokémon X | `0004000000055D00` |
| Pokémon Y | `0004000000055E00` |
| Pokémon Omega Ruby | `000400000011C400` |
| Pokémon Alpha Sapphire | `000400000011C500` |

It ships as a **`.3gx` game plugin** loaded by Luma3DS into the running game
process. The plugin reads game state out of the game's own memory, turns it
into English text, synthesises that text on-console with **eSpeak NG**, and
plays the result through the **CSND** audio service so it layers on top of the
game's own audio without disturbing it.

The goal is a player who cannot see the screen can start, play, and finish the
game unaided.

## Read this first

Detailed design and research notes live in **`AI docks/`** — one file per
concern. `CLAUDE.md` is the map; `AI docks/` is the territory. Before doing
substantive work in an area, read the matching doc:

| Doc | Read it when |
| --- | --- |
| `AI docks/README.md` | Index of everything below |
| `01-project-overview.md` | Orienting; scope and non-goals |
| `02-accessibility-design.md` | Adding/changing any user-facing behaviour |
| `03-target-games.md` | Anything version-, region-, or title-specific |
| `04-gen6-reverse-engineering.md` | Finding or using game memory/data |
| `05-plugin-architecture.md` | Touching plugin structure, hooks, threads |
| `06-tts-audio-pipeline.md` | Touching speech, audio, or eSpeak |
| `07-build-environment.md` | Build, toolchain, or dependency work |
| `08-repo-layout.md` | Adding files; where things belong |
| `09-coding-standards.md` | Writing any code |
| `10-testing-and-qa.md` | Verifying a change |
| `11-roadmap.md` | Deciding what to do next |
| `12-research-log.md` | Looking for a source or prior finding |
| `13-glossary.md` | Unfamiliar term |
| `15-home-menu-screen-reader.md` | Touching `browser/`, `plugin-screenreader/`, or system-UI reading |
| `14-legal-and-licensing.md` | Adding a dependency or distributing |

## Hard rules

1. **Never commit copyrighted game content.** No ROMs, no `code.bin`, no
   RomFS/ExeFS dumps, no extracted GARC archives, no ripped text or audio.
   Offsets, structure definitions, and research notes are fine; game data
   is not. See `14-legal-and-licensing.md`.
2. **Accessibility is translation, not transformation.** The mod exposes
   information the game already gives sighted players. It does not grant
   advantages sighted players lack (no RNG manipulation, no cheat codes, no
   revealing hidden information). Cheat functionality belongs in other
   projects.
3. **Speech must never block the game thread.** Synthesis runs on a dedicated
   worker thread. The frame callback only enqueues.
4. **Offsets are version-specific.** Every hardcoded address must go through
   the game/version dispatch layer and be tagged with the game and update
   version it was verified against. Never inline a raw address at a call site.
5. **Fail silent, never crash.** A bad read, missing file, or unavailable audio
   channel degrades gracefully. A plugin crash takes the game down with it and
   a blind user cannot read the exception screen.
6. **GPL-3.0.** eSpeak NG is GPLv3 and we link it, so this project is GPLv3.
   Check `14-legal-and-licensing.md` before adding any dependency.

## Repository layout

```
plugin/          The .3gx plugin (C++11, devkitARM)
  source/        Implementation
  include/       Headers
  data/          Assets compiled into the plugin
cmake/           Toolchain files for cross-compiling dependencies
scripts/         Bootstrap, build, and packaging scripts
third_party/     Fetched dependencies (gitignored, see scripts/bootstrap.sh)
tools/           Host-side helper tools
AI docks/        Design + research documentation
docs/            User-facing documentation
```

## Common commands

Run everything from **MSYS2** (`C:/devkitPro/msys2/msys2_shell.bat`) or Git
Bash with `scripts/env.sh` sourced.

```bash
source scripts/env.sh          # Set DEVKITPRO/DEVKITARM/PATH
scripts/bootstrap.sh           # One-time: install toolchain deps + fetch sources
scripts/build-espeak-3ds.sh    # Cross-compile libespeak-ng.a for ARM11
scripts/build-plugin.sh        # Build plugin/XYORASAccess.3gx
scripts/package.sh             # Produce an SD-card-ready luma.zip
```

Deploy: copy the produced `luma/plugins/<TitleID>/XYORASAccess.3gx` to the SD
card, one copy per supported title ID, and enable the plugin loader in
Rosalina (`L` + `Down` + `Select`).

## Toolchain facts

- devkitPro is installed at `C:/devkitPro`; inside MSYS2 it is `/opt/devkitpro`.
- The plugin builds with **devkitARM** against **libctrpf** (CTRPluginFramework
  0.8.x) and **libctru**, linked with `plugin/3gx.ld`, then converted to `.3gx`
  by `3gxtool`.
- `libctrpf` and `3gxtool` come from the ThePixellizerOSS pacman repositories,
  added to `/etc/pacman.conf` by `scripts/bootstrap.sh`.
- Audio uses **libcwav** + **libncsnd** (CSND backend) — the only audio path
  that works from inside a game process.
- ARM11 is `armv6k`, hard-float, little-endian. C++11, no RTTI, no exceptions.

## Working style for this repo

- Prefer adding a fact to the right `AI docks/` file over re-deriving it later.
  Every offset you confirm, every hook you land, every dead end you hit —
  write it down in `12-research-log.md`.
- When you cannot verify something on hardware, say so in the doc and mark it
  `UNVERIFIED`. Do not present a guessed offset as confirmed.
- Changes that affect what a player hears need a line in
  `02-accessibility-design.md` describing the intended utterance.
