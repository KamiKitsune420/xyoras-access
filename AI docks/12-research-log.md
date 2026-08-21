# 12 — Research log

Findings, sources, and dead ends. Append, do not rewrite — a superseded finding
is still useful context. Newest sections at the bottom.

---

## 2026-08-20 — Initial research pass

### How accessibility mods are made

Surveying existing blind-accessibility mods across platforms, the same handful
of techniques recur:

- **Screen-reader hand-off.** The mod extracts UI text and pushes it to the
  platform screen reader (NVDA/JAWS via Tolk on Windows, SAPI as a fallback).
  Standard on PC. Not available to us — the 3DS has no screen reader.
- **Built-in TTS.** Where no screen reader exists, the mod becomes one,
  synthesising speech itself. This is our situation.
- **Memory reading.** Reading the game's memory or frame buffer to detect state
  changes that produce no audio cue. The core technique for games with no mod
  API, which describes every 3DS retail game.
- **Positional audio cues.** Stereo/3D panning to indicate the direction of
  objectives, enemies, or walls.
- **Sonar / proximity scanning.** Pitch or repetition rate encoding distance to
  nearby obstacles or interactables.
- **Tile-based navigation with pathfinding.** For grid-based games, describing
  the tile grid and offering pathing assistance.

The stated design philosophy across these projects is **"translation, not
transformation"** — expose what the game already shows, do not change the
gameplay loop or grant an advantage. Adopted as rule 2 in `CLAUDE.md`.

Sources:
- AccessMods — https://github.com/AccessMods
- Blind-accessible games list — https://github.com/Molitvan/blind-accessible-games-list/
- AFB, "Blindness Accessibility in Video Games: A Deep Dive" — https://www.afb.org/aw/fall2023/Blindness-Accessibility-in-Video-Games-A-Deep-Dive
- PvZA11y — https://github.com/CG8516/PvZA11y
- DDLC Screen Reader Mod — https://github.com/lordluceus/DDLCScreenReaderMod

### The closest precedent: pokemon-access

`nuive/pokemon-access` makes Gen 1–3 Pokémon accessible to screen-reader users.
Its architecture is worth understanding because it solved the same *problem*
with completely different *constraints*.

- **Lua scripts inside VBA-ReRecording**, an emulator with a scripting API.
- **Memory reading**, not OCR — `gb.lua` and `gba.lua` hold the per-platform
  addresses, `pokemon.lua` orchestrates.
- **Output via Tolk** to the user's existing screen reader; the mod never
  synthesises audio itself.
- **Tile-based navigation** with an A* pathfinder (`a-star.lua`), plus a
  free-moving "camera" decoupled from the player so the map can be explored
  without walking.
- Supports Red/Blue/Yellow, Gold/Silver/Crystal, FireRed/LeafGreen/Emerald,
  in six languages, with ROM-hack support via swappable data folders.

Two features to steal outright: the **decoupled exploration camera** and the
**pathfinder**. Both are in `11-roadmap.md` Phase 5.

The thing we cannot copy is the delivery mechanism. There is no scripted
emulator for the 3DS, and no host screen reader on the console, which is what
forces the plugin-plus-on-device-TTS design.

Source: https://github.com/nuive/pokemon-access

Also noted: an accessibility patch exists for a BDSP ROM hack
(https://www.nexusmods.com/pokemonbdsp/mods/54), confirming appetite for
accessible Pokémon beyond the emulated generations.

### Delivery mechanism: 3GX plugins

**3GX** ("3DS Game eXtension") loads compiled C/C++/ASM into a running game
process. Descended from NTR CFW's `.plg`. The loader was merged into official
Luma3DS in July 2023, so no custom firmware fork is needed any more.

- Plugins live at `sd:/luma/plugins/<TitleID>/*.3gx`; `default.3gx` in
  `sd:/luma/plugins/` loads for every title.
- On New 3DS the plugin loader must be enabled from Rosalina
  (`L` + `Down` + `Select`).
- `3gxtool` builds a `.3gx` from a linked `.elf` plus a `.plgInfo`.

Sources:
- Hacks Guide, 3DS game plugins / 3GX — https://wiki.hacks.guide/wiki/3DS:Game_plugins/3GX
- Luma3DS 3GX Loader Edition — https://www.gamebrew.org/wiki/Luma3DS_3GX_Loader_Edition_3DS
- 3gxtool — https://www.gamebrew.org/wiki/3gxtool_3DS

### Framework: CTRPluginFramework

CTRPF gives plugins memory access, hooks, input, file IO, threading, an OSD,
and a menu system.

Key discovery about **which version to use**: the canonical repository is on
GitLab at `thepixellizeross/ctrpluginframework`. Its `master` branch was last
touched in **2020** and has **no sound engine**. The `develop` branch (active,
last commit 2026-05-22) contains:

```
Library/include/CTRPluginFramework/Sound/Sound.hpp
Library/include/CTRPluginFramework/Sound/SoundEngine.hpp
Library/include/CTRPluginFrameworkImpl/Sound/SoundEngineImpl.hpp
```

The pacman package `libctrpf 0.8.0.r1444-1` matches the develop branch's commit
count, so **installing from the ThePixellizerOSS pacman repo gets the version
with sound**. Vendored copies of libctrpf found in community plugin repos are
older and lack it. This matters: the entire audio design depends on that API.

Confirmed API surface (from the headers):

- `Process::Read8/16/32/64`, `ReadString`, `Write*`, `CheckAddress`,
  `CheckRegion`, `GetTitleID`, `GetVersion`, `Pause`, `Play`, `Patch`.
- `Hook` — `Initialize(target, callback, returnAddr)`, `Enable()`, `Disable()`,
  with flags for executing the overwritten instruction before or after the
  callback. **Maximum 91 enabled hooks.** Position-dependent target
  instructions cannot be auto-re-executed.
- `Sound(const std::string& bcwavFile, int maxSimultPlays)` and
  `Sound(const u8* bcwavBuffer, int maxSimultPlays)` — **the memory-buffer
  constructor is what makes synthesised speech possible**. Plus `Play()`,
  `Stop()`, `SetVolume`, `SetPan`, and a `CWAVStatus` enum including
  `NO_CHANNEL_AVAILABLE` and `GOING_TO_SLEEP`.
- `DirectSoundModifiers` — speed multiplier, per-channel volume, ignore volume
  slider, force speaker output, play during sleep.

Sources:
- https://gitlab.com/thepixellizeross/ctrpluginframework
- https://www.gamebrew.org/wiki/CTRPluginFramework_3DS
- https://github.com/Nanquitas/CTRPluginFramework-BlankTemplate

### The audio question — resolved

This was the make-or-break unknown: **can a plugin play audio while a game
runs?**

Yes. The 3DS has two audio services:

- **DSP** — used by normal applications. The game owns it.
- **CSND** — used by *applets* to play audio on top of a running or suspended
  application "without causing any interferences".

**libcwav** (https://github.com/PabloMK7/libcwav) is a BCWAV playback library
explicitly designed for "non-application environments, such as *3GX game
plugins* or *applets*", with CSND support for exactly this reason. It requires
**libncsnd** (https://github.com/mariohackandglitch/libncsnd).

Supported encodings: **PCM8, PCM16**, DSP ADPCM (DSP backend only), IMA ADPCM
(CSND backend only). We use PCM16 — no encode step, and it works on CSND.

Caveat from the library's own docs: with CSND you must handle APT events via
`cwavDoAptHook()` / `cwavNotifyAptEvent()` for app suspend, sleep, and exit.

BCWAV files are loaded fully into linear RAM (unlike streamed BCSTM), which is
fine for a few seconds of speech.

CWAV format reference: https://www.3dbrew.org/wiki/BCWAV
Tooling for offline conversion: https://github.com/mariohackandglitch/cwavtool

### TTS engine: eSpeak NG

Chosen for now, explicitly replaceable. Formant synthesis, 100+ languages, tiny
footprint, portable C with CMake. Already ported to the Nintendo DS by the
homebrew community, which is a strong signal for ARM11 viability — though that
port only wrote `.wav` files rather than playing live.

A local clone already exists at
`~/Documents/llm experiments/misk/wii project/espeak-ng`, a clean upstream
checkout with a devkitPPC Wii cross-compile toolchain added. Its
`build_wii.sh` gives us the known-good option set to mirror for 3DS:

```
-DBUILD_SHARED_LIBS=OFF -DENABLE_TESTS=OFF -DCOMPILE_INTONATIONS=OFF
-DUSE_MBROLA=OFF -DUSE_ASYNC=OFF -DUSE_LIBPCAUDIO=OFF -DUSE_LIBSONIC=OFF
-DUSE_KLATT=ON
```

Sources:
- https://github.com/espeak-ng/espeak-ng
- eSpeak on Nintendo DS — https://www.gamebrew.org/wiki/ESpeak
- libctru ndsp — https://github.com/devkitPro/libctru/tree/master/libctru/source/ndsp

### Gen 6 game structure

**Title IDs** (confirmed from the folder layout community plugins deploy to):

| Game | Title ID |
| --- | --- |
| X | `0004000000055D00` |
| Y | `0004000000055E00` |
| Omega Ruby | `000400000011C400` |
| Alpha Sapphire | `000400000011C500` |

**No decompilation project exists for Gen 6.** Unlike the GBA and DS
generations, there is no `pokeemerald`-style decomp to read. All code
understanding must come from disassembling `code.bin`.

**RomFS/ExeFS.** RomFS holds assets as `a/x/y/z` files, mostly **GARC**
archives. ExeFS holds `code.bin`. Dumping is done with GodMode9 from the user's
own cartridge.

Known GARC locations (ORAS unless noted): Pokémon models `a/0/0/8`, trainer
overworlds `a/0/2/1`, trainer battle models `a/1/3/3`, map models `a/0/3/9`,
map textures `a/0/1/4`. For XY, game text sits in **two** archives — `a/0/7/4`
and `a/0/8/2` — and system updates make the game read the latter. Anyone
editing or reading text must know which one the running version uses.

**PK6 structure.** 232 bytes stored, 260 in party. Encrypted with an LCRNG
seeded by the **encryption constant** at offset 0, with four **56-byte** blocks
shuffled by an order derived from that same constant. This differs from Gen 4/5
which seeded from the PID with 32-byte blocks. Same decryption whether in a box
or in RAM. PKHeX's `PokeCrypto.cs` is the reference implementation.

**RAM is heap-allocated.** Gen 6 does not use fixed addresses for most live
data — structures move between boots. Community practice is pointer chains
found via memory dumps and pointer searchers, or breakpoints in IDA Pro to
catch the accessing instruction. `CTR-Heap-Mapper` helps map heap block starts.
Pokémon data in RAM is encrypted exactly as in the save.

Sources:
- Project Pokémon, XY file system — https://projectpokemon.org/home/docs/gen-6/xy-file-system-r89/
- Project Pokémon, ORAS file system — https://projectpokemon.org/home/docs/gen-6/oras-file-system-r32/
- Project Pokémon, PKM structure (X/Y) — https://projectpokemon.org/home/docs/gen-6/pkm-structure-xy-r66/
- Project Pokémon, XY/ORAS RAM research thread — https://projectpokemon.org/home/forums/topic/37310-pokemon-xyoras-ram-research-thread/
- RomFS file locations gist — https://gist.github.com/LunNova/367a195712dc9cfc6f6c
- pk3DS — https://github.com/kwsch/pk3DS
- PKHeX `PokeCrypto.cs` — https://github.com/kwsch/PKHeX/blob/master/PKHeX.Core/PKM/Util/PokeCrypto.cs
- CTR-Heap-Mapper — https://gbatemp.net/threads/ctr-heap-mapper-mapping-your-games-memory-made-simple.680407/

### Prior art: Gen 6 CTRPF plugins

Three related community projects, all GPL-3.0, form a lineage:

1. `semaj14/Multi-PokemonFramework` — abandoned, the original.
2. `biometrix76/AlolanCTRPluginFramework` — Gen 7 (Sun/Moon/USUM).
3. `biometrix76/Gen6CTRPluginFramework` — **Gen 6, actively maintained.**

These are cheat/save-editing plugins, not accessibility tools, so their
*purpose* is orthogonal to ours — but their *plumbing* is directly reusable:

- A working devkitARM Makefile for a 3GX plugin, including the `3gx.ld` linker
  script and the `3gxtool` invocation.
- A `.plgInfo` example (`MemorySize: 5MiB`, version fields, compatibility).
- The `AutoGameSet(kalosValue, hoennValue)` / `AutoGame(first, second)`
  dispatch pattern for XY-vs-ORAS and X-vs-Y differences. Adopted.
- A complete `PK6` C struct definition.
- A large table of verified Gen 6 memory offsets, transcribed into
  `04-gen6-reverse-engineering.md` and marked `UNVERIFIED` pending our own
  confirmation.
- Their build setup requires adding the ThePixellizerOSS pacman repos, which is
  what led to finding `libctrpf 0.8.0.r1444`.

Since we are also GPL-3.0, borrowing code from these is permitted with
attribution. Record any borrowing here and in the source file.

Sources:
- https://github.com/biometrix76/Gen6CTRPluginFramework
- https://github.com/biometrix76/AlolanCTRPluginFramework
- https://github.com/semaj14/Multi-PokemonFramework

### Dead ends and rejected options

- **Azahar/Citra Lua scripting.** Investigated as an easier path mirroring
  `pokemon-access`. Azahar (the maintained Citra successor, formed from
  PabloMK7's Citra fork plus Lime3DS) has **no scripting API, no Lua, and no
  plugin system**. Rejected — it also would have required players to use a PC.
  Azahar remains useful as a memory-inspection research tool.
- **DSP audio from the plugin.** Rejected in favour of CSND; the game owns the
  DSP service and libcwav's own documentation recommends CSND for 3GX plugins.
- **Vendoring libctrpf from a community plugin repo.** Rejected — the vendored
  copies predate the sound engine.
- **Shipping extracted game text.** Rejected on copyright grounds. We ship our
  own factual name tables and read dynamic text from RAM at runtime.
- **CTRPF `master` branch.** Rejected — 2020-era, no sound.

### Environment audit

Verified present on this machine on 2026-08-20:

- devkitPro at `C:/devkitPro` with devkitARM, devkitPPC, devkitA64, libctru,
  portlibs, and `cmake/3DS.cmake`.
- devkitPro MSYS2 with `pacman.exe`; `pacman.conf` had only `dkp-libs`,
  `dkp-windows`, and `msys` — the ThePixellizerOSS repos are added by
  `scripts/bootstrap.sh`.
- CMake and GNU make from the MSYS2 tree; Python 3.12; Git; `gh` 2.92.0
  authenticated.
- `arm-none-eabi-gcc` is **not** on the default Git Bash PATH; it lives in
  `C:/devkitPro/devkitARM/bin`. Handled by `scripts/env.sh`.
- Package versions confirmed available: `libctrpf 0.8.0.r1444-1` (any),
  `3gxtool 1.3-1` (x86_64/win).

---

## Template for new entries

```
## YYYY-MM-DD — <topic>

**Question:** what were you trying to find out?
**Method:** how did you look?
**Finding:** what is true, and how confident are you?
**Verified on:** game, update version, console (or "not verified").
**Consequence:** what changes in the code or the docs because of this?
**Sources:** links.
```

---

## 2026-08-20 — Build environment brought up

**Question:** does the whole toolchain actually work end to end, and what
breaks along the way?

**Finding:** it works. `scripts/build-espeak-3ds.sh` produces a 520 KB
`libespeak-ng.a` for ARM11, `scripts/build-plugin.sh` produces a 1.2 MB
`XYORASAccess.3gx`, and `scripts/package.sh` produces a 2.5 MB `luma.zip`
laid out for the SD card. Not yet run on hardware.

Six things bit on the way, all now handled in the scripts. Recording them
because every one of them would otherwise be rediscovered the hard way.

### 1. devkitPro's MSYS2 and Git Bash do not mix

devkitPro ships its own MSYS2 linked against its own `msys-2.0.dll`. Git Bash
ships a different one. Running devkitPro's `make` from Git Bash breaks three
ways at once:

- Exported environment variables are not inherited, so every devkitPro Makefile
  stops at "Please set DEVKITARM in your environment" **even though it is set**
  and visible in `env`.
- Sub-makes spawn Git Bash, which has no `/opt/devkitpro` mount, so recipes
  fail with paths that resolve to nothing.
- `PATH` does not carry across either: `arm-none-eabi-gcc: command not found`
  while `command -v arm-none-eabi-gcc` succeeds in the parent shell.

Passing variables on the make command line fixes only the first. The real fix
is to re-execute the script inside devkitPro's own MSYS2, which is what
`scripts/msys-guard.sh` now does automatically.

### 2. devkitPro's build rules cannot handle spaces in paths

`base_rules` and `3ds_rules` do not quote `$(CURDIR)`. The project was
originally at `.../GitHub/xyoras access/` and the build split the path at the
space:

```
make[1]: /home/adels/Documents/GitHub/xyoras: No such file or directory
```

Confirmed both directions: identical sources build from a space-free path and
fail from a spaced one. The project directory was renamed to `xyoras-access`.
**Do not put this project anywhere with a space in the path.**

### 3. TMPDIR must be set

Without it, `sed` and `gcc` try to write temporary files to `C:\WINDOWS` and
fail with a permission error:

```
Cannot create temporary file in C:\WINDOWS\: Permission denied
```

`msys-guard.sh` exports `TMPDIR=/tmp`. The previous Wii build script for the
same library carried the same line, so this is a long-standing devkitPro-on-
Windows quirk rather than anything new.

### 4. libctrpf does not ship `types.h`

The pacman package installs `CTRPluginFramework.hpp` and the
`CTRPluginFramework/` tree, but its headers `#include "types.h"` and that file
is **not** in the package. Plugin projects are expected to supply it — the
community plugin repos all carry a copy in their own include directory.

Taken from CTRPF `develop` (`Library/include/types.h` and `csvc.h`) into
`plugin/include/`. Note the integer typedefs (`u8`, `u32`, `s16`, ...) are in
the **global** namespace, not inside `CTRPluginFramework`.

### 5. eSpeak links as three static libraries, not one

`libespeak-ng.a` alone leaves hundreds of undefined `ucd_*` references. The
build also produces `libucd.a` (Unicode character database) and
`libspeechPlayer.a`, and both must be on the link line after `-lespeak-ng`.

### 6. eSpeak voice data cannot be cross-compiled

`phondata`, `phontab`, `phonindex`, and `en_dict` are generated by **host**
tools during a normal eSpeak build. Cross-compiling never runs them, so the
source tree yields only a stub data directory — 146 KB of voice variants and
nothing that can actually speak.

Fix: take the compiled data from an eSpeak NG **installation** of the same
version. The library and its data must match, so `bootstrap.sh` now pins the
source to tag **1.52.0**, matching the eSpeak NG 1.52.0 installed on this
machine at `C:/Program Files/eSpeak NG/espeak-ng-data`. Staged data is 988 KB.
`ESPEAK_SYSTEM_DATA` overrides the search.

This also means **no host C compiler is needed** anywhere in the build.

### Package versions installed

| Package | Version | Source |
| --- | --- | --- |
| `libctrpf` | 0.8.0.r1444-1 | thepixellizeross-lib (pacman) |
| `3gxtool` | 1.3-1 | thepixellizeross-win (pacman) |
| `libcwav` | git master | built from source, installed to `$DEVKITPRO` |
| `libncsnd` | git master | built from source, installed to `$DEVKITPRO` |
| `espeak-ng` | tag 1.52.0 | pinned checkout |
| devkitARM GCC | 16.1.0 | devkitPro |

**Consequence:** Phase 0 tasks 0.5–0.7 are done. 0.8 (loads on hardware)
still needs a console.

---

## 2026-08-20 — Can the plugin be tested in an emulator? No.

**Question:** hardware was unavailable (no charger). Can Azahar substitute?

**Method:** built the plugin with a WAV-file audio backend and a marker-file
self-test so nothing needed to be heard or navigated, deployed to Azahar's
emulated SD at `sdmc/luma/plugins/<TitleID>/`, enabled the plugin loader, and
ran Pokémon X while reading `azahar_log.txt`.

**Finding: no, and it is not fixable from our side.** Three layers, two of
which are unimplemented emulation:

| Layer | Azahar | Evidence |
| --- | --- | --- |
| Loads the `.3gx`, parses `.plgInfo` | works | `PLGLDR: Trying to load plugin - Title: XYORAS Access - Author: XYORAS Access contributors` |
| Runs CTRPluginFramework | **fails** | `Kernel.SVC: unimplemented SVC function 80 CustomBackdoor(..)` |
| Plays audio via CSND | **fails** | every `CSND_SND::*` call logs `(STUBBED)` |

CTRPF got as far as initialising its own sound engine (`CSND_SND::Initialize`,
`AcquireSoundChannels` at t=2.33 — distinct from the game's own DSP init at
t=3.09), then called **`svcCustomBackdoor`, Luma3DS's custom SVC 0x80**. Azahar
does not implement it. It is called exactly once and the plugin does nothing
afterwards: zero filesystem access to `/xyoras-access`, so eSpeak never
initialised and our `main()` never reached our own code. The game itself booted
and ran normally for 103 seconds.

This is the deeper reason emulator testing fails. CSND being stubbed would
already make speech inaudible, but the plugin does not even get that far —
CTRPF is built against Luma's custom SVCs and an emulator that lacks them
cannot host it.

**Consequence:**

- `10-testing-and-qa.md` was right that plugin testing needs hardware, but for
  the wrong reason. Corrected there.
- Azahar remains useful for **running the game** — which is all Phase 2 offset
  research needs.
- Two things built for this attempt are keepers, because both are useful on
  hardware:
  - **WAV audio backend** (`speech/audio_wav.cpp`) — writes utterances to
    `/xyoras-access/speech/*.wav` instead of playing them. On hardware this
    tells you instantly whether silence is a synthesis problem or a playback
    problem.
  - **Startup self-test** (`diagnostics.cpp`) — triggered by a marker file
    `/xyoras-access/dump-audio`, writes `/xyoras-access/diagnostics.txt` with
    the detected game, whether eSpeak started, sample rate, sample count,
    synthesis time, and a realtime factor. That last number is exactly the
    Phase 1 exit measurement, and a blind user can produce it by creating one
    empty file and sending back one text file.

**Also unresolved and now urgent:** eSpeak reads its voice data with plain
`fopen`, and whether newlib's `sdmc:` device is mounted inside a game process
is still unverified — the emulator never got far enough to answer it. CTRPF
provides its own `File` API that uses the game's FS session, and community
plugins use it exclusively, which is weak evidence that `fopen` may not work.
If it does not, eSpeak needs its data supplied another way. **Verify this
first when hardware is available** — the whole speech design rests on it.

---

## 2026-08-20 — Host tests found a real design contradiction

**Question:** with the emulator ruled out, what can be tested without hardware?

**Method:** built `scripts/host-test.sh`, which compiles the plugin's own logic
natively and runs it. The key piece is `plugin/include/xyoras/sync.hpp`, which
maps `Mutex`, `Lock`, and `WakeEvent` onto CTRPF's primitives on the 3DS and
onto `std::mutex` on the host, so `queue.cpp` compiles unchanged for both. The
tests exercise the shipped source, not a copy — testing a copy tests nothing.

MSVC is located through `vswhere`, so Visual Studio does not need to be on
`PATH`. Two quirks worth recording: `python3` is not on `PATH` on this machine
(only `python`), and quoting a `vcvars64.bat` call plus a `cl` invocation
through `cmd //c` from Git Bash cannot be made to work — the script writes a
throwaway `.bat` instead.

**Finding: the queue had a real bug, and it came straight from an ambiguity in
the design doc.** Two rules in `02-accessibility-design.md` contradict:

- `CRITICAL` — "cancels lower priorities"
- `DIALOGUE` — "queued in order, never dropped"

`DIALOGUE` is lower than `CRITICAL`, so the implementation did what the first
rule said: a "your Pokémon fainted" message **silently deleted queued story
dialogue**. On hardware this would have shown up as story lines occasionally
vanishing during battles — intermittent, unreproducible, and very hard to
attribute.

Resolved in favour of dialogue. `CRITICAL` now clears only `UI` and `AMBIENT`
and raises the cancel flag, so it is still heard immediately; the priority sort
already places it ahead of pending dialogue, so nothing is lost by keeping the
line. `INTERRUPT` still clears everything including dialogue, because the
player explicitly asked for that and message history can recover it. The
distinction is consent: automatic events do not get to discard the story,
player-initiated ones do.

**Consequence:** `02-accessibility-design.md` now states the precedence
explicitly instead of leaving two rules to collide. Current coverage is 60
checks across two suites, all passing.

**Worth noting for its own sake:** this bug was invisible to code review — both
rules read as obviously correct in isolation, and the implementation faithfully
matched one of them. It took writing down an expectation as an executable
assertion to notice they could not both hold.

---

## 2026-08-20 — Speech works. Two findings, one of them a near-miss.

**Correction to the previous entry.** It claimed CTRPF cannot run under Azahar
because `svcCustomBackdoor` is unimplemented. **That was wrong.** The
unimplemented SVC is logged but is not fatal: with checkpoint instrumentation
in place, the plugin's `main()` demonstrably runs. The earlier conclusion
mistook "the plugin produced no files" for "the plugin did not run", and two
different causes fitted the same evidence.

### Finding 1: stdio does not work in a game process

**`fopen` fails inside a plugin.** Measured directly:

```
reached: fopen FAILED before mount
reached: fsInit ok
reached: archiveMountSdmc ok
reached: fopen WORKS after mount
```

A homebrew application gets the `sdmc:` devoptab registered by libctru's
startup before `main()`. A plugin is injected into an already-running game and
never goes through that path, so stdio cannot resolve `sdmc:` at all.

This nearly sank the whole speech design: **eSpeak reads every one of its data
files -- `phondata`, `phontab`, `phonindex`, `en_dict` -- through stdio.**

The fix is one call, made before speech starts (`platform.cpp`):

```c
fsInit();
archiveMountSdmc();
```

Note `archiveMountSdmc`, not `sdmcInit`. The latter appears in older
documentation and community code but is not exported by current libctru at all
-- `arm-none-eabi-nm` on `libctru.a` shows no such symbol.

### Finding 2: eSpeak hangs rather than failing when data is missing

With no readable voice data, `espeak_Initialize` **never returns**. It does not
return an error; it hangs, on the plugin's own thread, taking the mod down
silently. For a blind player that is the worst possible failure: no banner, no
speech, no way to discover why.

Guarded in `synth_espeak.cpp` by checking the four required files are openable
before handing control to eSpeak. Verified by deliberately hiding the voice
data:

```
reached: espeak: voice data missing, refusing to initialise
reached: speech: synth Init FAILED
reached: speech FAILED to start
reached: entering menu loop          <- game still playable
```

The diagnostics report then names the missing files. This is rule 5 in
`CLAUDE.md` demonstrated rather than assumed.

### The pipeline works end to end

```
plain fopen    : WORKING
subsystem      : started
sample rate    : 22050 Hz
synthesis      : ok
samples        : 128199
audio length   : 5814 ms
synth time     : 334 ms
realtime factor: 17.40x
playback       : accepted
```

Two `.wav` files were produced and verified to contain real speech: 3.24 s and
6.15 s, RMS ~3300, peak ~29000 (no clipping), 57% voiced frames -- a normal
speech duty cycle, not noise. Copies kept in `dist/sample-speech/`.

**Read the realtime factor with care.** It is measured under emulation, and
while Azahar derives guest time from emulated cycle counts rather than host
wall-clock, dynarmic's cycle accounting is approximate. Treat 17x as
"comfortably faster than real time, probably by a wide margin" and not as a
hardware number. Old 3DS runs at roughly a third of New 3DS's clock, so even a
large error leaves headroom -- but the Phase 1 exit measurement still has to be
taken on hardware.

### What this changes

Azahar is far more useful than the previous entry concluded. It cannot play
audio (CSND is stubbed) and it cannot be trusted for timing, but it **can** run
the plugin, run CTRPF, run eSpeak, and exercise file access. Combined with the
WAV backend and the checkpoint instrumentation, that covers most of the plugin
outside of playback itself.

Standing caveat: everything here is emulator-observed. The `sdmc:` finding is
mechanism-level (libctru startup code that plainly does not run in a plugin) so
it should hold on hardware, but confirm it there before treating it as settled.

---

## 2026-08-20 — CSND path implemented and verified as far as an emulator allows

**Question:** the user has no hardware access. Can the CSND playback path be
verified at all?

**Method:** searched for any emulator that implements CSND audio. **None does**
-- Panda3DS's is a 104-line stub, and every Citra descendant (Azahar, CitraVR,
Cytrus, mandarine-neo, libretro) shares the same stubbed `csnd_snd.cpp`. No
issues, no PRs, nobody has attempted it.

But Azahar's CSND is only *unfinished*, not absent: 535 lines that fully parse
the command protocol and track every channel's encoding, buffer address, size,
sample rate, volume and loop mode. The gap is literally:

```cpp
case CommandId::Start:
    // TODO: start/stop the sound
    break;
```

So while it cannot *play*, it can *receive and validate* everything we send.
That is enough to prove the container and the library integration are correct.

Implemented the real path (`bcwav.hpp`, `audio_cwav.cpp`) and ran it.

### Finding: `linearAlloc` does not work in a plugin either

Same root cause as the `sdmc:` finding: libctru's `linearAlloc` draws from a
linear heap created during **application** startup, which a plugin never
executes. It returns null every time.

```
reached: cwav: linearAlloc FAILED
```

This matters because CSND reads its buffers by **physical** address, so the
audio data must be physically contiguous. Fixed in `platform.cpp` by asking the
kernel directly:

```c
svcControlMemory(&addr, 0, 0, PageAlign(size), MEMOP_ALLOC_LINEAR, MEMPERM_READWRITE);
```

which is what libctru's own heap setup uses underneath. Sizes must be
page-aligned or the call is rejected.

**This is now the second instance of the same class of bug**, and it is worth
generalising: *anything libctru sets up during application startup is absent in
a plugin.* So far that is the `sdmc:` devoptab and the linear heap. Expect more
-- romfs and the socket service are likely candidates. When a libctru API
mysteriously fails in a plugin, check whether it depends on `__appInit` before
assuming anything else.

### Result: the CSND path works

After the fix:

```
reached: cwav: play OK
playback       : accepted

CSND_SND::Initialize           x1
CSND_SND::AcquireSoundChannels x1
CSND_SND::ExecuteCommands      x5
Unimplemented command ID       0
```

libcwav accepted our BCWAV, acquired a channel, and issued five command batches
that Azahar parsed **without a single unrecognised command**. That verifies:

- the BCWAV container we build is valid (libcwav's parser accepted it);
- `svcConvertVAToPA` works as the VA->PA callback in a plugin;
- linear allocation and cache flushing are correct enough to be accepted;
- channel acquisition succeeds and the command stream is well-formed.

Azahar's channel mask (`0xFFFFFF00`) matches real hardware, where CSND owns the
upper channels.

**What remains unverified is precisely one thing: whether the samples sound
right coming out of a speaker.** Everything up to the emulator's missing mixer
is now confirmed. That is as far as any emulator can take this without
implementing CSND audio output, which nobody has done.

### BCWAV format notes

Written from libcwav's own parser rather than from documentation, since the
parser is what actually has to accept the file. Non-obvious points:

- `version` must be exactly `0x02010000`; `blockCount` exactly 2.
- The INFO block's own `header.size` must equal the size in the file header's
  reference, or the load fails with `INVAID_INFO_BLOCK`.
- Channel-info reference offsets are relative to **`&channelInfoRefs.count`**,
  not to the file or the block.
- The sample reference offset is relative to the DATA block's `data` **field**
  -- i.e. 8 bytes past the block start.

`tools/host-test/test_bcwav.cpp` asserts all of this against the generated
bytes: 32 checks mirroring each condition libcwav tests. Worth having, because
a single wrong field yields an opaque status code whose only symptom on a
console is silence.

---

## 2026-08-21 — Physical address verification; emulator build blocked on disk

**Goal:** implement CSND audio output in Azahar so the speech could actually be
heard, closing the last unverified link.

**Blocked, deliberately.** The only drive on this machine is at **100% (9.3 GB
free of 931 GB)**. An Azahar clone with submodules is 1-2 GB and an MSVC build
of a Citra-class project is several more. Attempting it unattended risked
filling the system drive with nobody present to intervene, which is a worse
outcome than not having the test. Not attempted; left for a time when there is
headroom.

### What was verified instead

The one remaining *checkable* link was the physical address. libcwav passes
CSND the **physical** address of the sample data, obtained through the VA->PA
callback we supply. If that conversion silently failed, CSND would read from
the wrong place and play noise -- and every check made so far would still pass,
because both the container and the command stream would be perfectly valid.

Measured from inside the plugin:

```
rate=22050 samples=71473  bytes=142946 va=16B48088 pa=22B48088 (FCRAM)
rate=22050 samples=133471 bytes=266942 va=16B6B088 pa=22B6B088 (FCRAM)
```

Both deltas are exactly **0x0C000000**, which is the documented 3DS linear
mapping (linear VA base `0x14000000` -> PA base `0x20000000`), and the low 24
bits are identical in each case. So `svcConvertVAToPA` is returning the correct
translation, not merely a plausible-looking number, and the buffer sits in
FCRAM where CSND can reach it.

### State of verification

| Link | Status | How |
| --- | --- | --- |
| PCM is real speech | verified | WAV dump analysed: 3.2 s / 6.2 s, RMS ~3300, peak ~29000 no clipping, 57% voiced |
| BCWAV container valid | verified | 32 host checks mirroring libcwav's parser, plus `cwavLoad` returning SUCCESS |
| libcwav accepts and plays | verified | `cwavPlay` SUCCESS, channel acquired |
| CSND command stream well-formed | verified | 5 `ExecuteCommands` batches, 0 unrecognised commands |
| Sample address reaching CSND | verified | VA->PA delta exactly 0x0C000000, lands in FCRAM |
| **Audible output** | **unverified** | needs CSND mixing: no emulator implements it, and hardware is unavailable |

Everything that can be checked without either a console or a patched emulator
has now been checked. The remaining risk is confined to the mixer itself --
whether correct samples at a correct address, at a correct rate, produce the
expected sound.

---

## 2026-08-21 — CSND output verified: byte-for-byte identical

**Goal:** verify the one remaining link -- that the audio reaching CSND is the
audio eSpeak produced. No emulator implements CSND output, so this meant
writing it.

**Method:** built Azahar from source (GPL-2.0) with a patch that captures what
a CSND channel would play. Patch and full notes in
`tools/azahar-csnd-patch/`.

### Result

```
CSND tap (what the hardware would receive)
  samples  71473   RMS 3261   peak 29427   voiced 57%
plugin dump (what eSpeak produced)
  samples  71473   RMS 3261   peak 29427   voiced 57%

comparing tap vs reference
  IDENTICAL across all 71473 samples
```

Reproduced twice: once against a reference captured in an earlier session, and
again against one captured from the same build under identical conditions.

The physical address also matches from both ends independently -- the plugin
reported handing over `0x22B48088`, and the emulator reports receiving
`0x22B48088`.

**This closes the chain.** PCM -> BCWAV -> linear allocation -> cache flush ->
VA->PA conversion -> libcwav -> CSND preserves the audio exactly. The only
thing now unverified is the DAC itself.

### Three findings worth keeping

**1. Azahar has no SDL frontend.** `citra_cli` is argument parsing only, and
`citra_meta/main.cpp` carries `#error "citra_meta is somehow building with no
frontend. This should be impossible!"`. Qt is mandatory. It is not a real
obstacle -- `USE_SYSTEM_QT=OFF` makes CMake download Qt 6.10.3 itself -- but a
Qt-free build cannot work, and discovering that after a long compile is
avoidable.

**2. libcwav never issues `CommandId::Start`.** It uses `ConfigureChannel`
(0x00E), the combined configure-and-start command, whose `enable_playback`
branch has its own separate TODO. The first version of the patch changed only
`Start`, compiled cleanly, ran, and produced nothing at all. Two TODOs, and the
obvious one is the wrong one.

**3. CSND's `sample_rate` is a timer divider, not Hz.** The hardware computes
the rate as `0x03FEC3FC / divider`. Our 22050 Hz audio reports `3039`
(`0x03FEC3FC / 22050 = 3039`). This briefly looked like a serious bug in our
own code; it was neither a bug nor ours.

### A side observation: interruption demonstrably works

The second utterance came out a different length on every run -- 128199,
133471, 135695 samples. That is not nondeterminism in eSpeak. Startup queues
three utterances (banner, untested-version warning, self-test phrase) and the
self-test's `Interrupt` cancels synthesis mid-utterance, so the warning is
truncated wherever the worker happened to be. Unplanned, but it is direct
evidence that cancellation reaches into an in-progress synthesis rather than
merely dropping queued items.

### Cost

About two hours: ~1.5 GB clone, Qt auto-download, ~2100 objects. Two false
starts (Qt-free build, wrong TODO) plus repeated loss of the build when its
launching shell was killed -- fixed by launching detached so the build outlives
the process that started it.

---

## 2026-08-21 — Gen 6 is 84 CRO modules, not one executable

**Question:** where is the message-box render path?

**Method:** dumped the decrypted, LZSS-decompressed executable by adding an
env-gated hook to Azahar's NCCH loader (`XYORAS_DUMP_CODE`), then examined its
strings. The dump is 5,156,864 bytes. It is copyrighted game code and lives
outside the repository; only observations are recorded here.

**Finding: most game logic is not in `code.bin` at all.** The executable
references **84 dynamically-loaded CRO modules**, one per subsystem:

| Module | Subsystem |
| --- | --- |
| `DllField.cro` | Overworld |
| `DllBattle.cro` | Battles |
| **`DllDialogCommon.cro`** | **Common dialogue -- the likely message box** |
| `DllPokeList.cro` | Party list |
| `DllBag.cro` | Bag |
| `DllStatus.cro` | Pokemon summary |
| `DllBox.cro` | Storage |
| `DllZukan.cro` | Pokedex |
| `DllStartMenu.cro` | Start menu |
| `DllTownmap.cro` | Town map |
| `DllStrInput.cro` | Text entry |
| `DllNumberInput.cro` | Number entry |
| `DllConfig.cro` | Options |
| `DllTitle.cro`, `DllIntro.cro`, `DllLangSelect.cro` | Boot sequence |
| `DllPss*.cro` (9 modules) | Player Search System |
| `DllField*.cro` (6 more) | Individual field events |

Full list preserved in `04-gen6-reverse-engineering.md`.

**Why this matters more than it first appears.** The address table this project
is built around assumes fixed addresses in a single executable. That model is
wrong for anything living in a CRO:

- CROs are loaded and unloaded on demand, so a module is only in memory while
  its subsystem is active.
- They are relocated at load time, so their addresses are **not fixed** across
  boots. Azahar's own log shows this directly:
  `RO::LoadCRO: CRO "DllLangSelect" loaded at 0x006A5000`.
- A hook into `DllDialogCommon.cro` must therefore be installed *after* the
  module loads, at an address computed from that module's base -- not from a
  constant.

So the plan of "find the message render function's address" needs replacing
with "find the module, learn where it was loaded this time, and hook at
base + offset". The dispatch layer needs a CRO-aware level above the existing
XY/ORAS split.

This also explains something noticed earlier and not understood: the inherited
community offsets all sit in two narrow bands (`0x8C7xxxx`/`0x81FFxxx`). Those
are static allocations reachable from `code.bin`, which is exactly the subset a
fixed-address cheat table *can* address. Everything else needed a different
technique, which is presumably why nobody had it.

### Also found

Developer paths survive in the binary, confirming Game Freak's library layout:

```
c:\homehudson\xy_project\prog\src/system/motion/Motion.cpp
c:\home\gflib_cpp_final\gflib\prog\include\base/gfl_Singleton.h
c:\home\gflib_cpp_final\gflnet\prog\src/...        (24 paths)
```

`gflib` is Game Freak's engine library and `gfl_` is its symbol prefix -- useful
when reading disassembly. Note `gfl_Singleton.h`: major subsystems are
singletons, so there is likely a small set of global instance pointers rather
than parameters threaded through call chains.

Most "Message"-containing strings are Nintendo SDK noise (`nn::nex` networking,
UDS local wireless), not game text. `rom:/DllPssMessageWindow.cro` is the PSS
chat window, not the field message box.

**Next:** CROs carry export tables and Azahar already parses them
(`CROHelper::ApplyModuleExport`). If those exports are named, they may identify
the message entry point directly. That is the thread to pull.

---

## 2026-08-21 — CRO exports are a dead end; save validation inconclusive

### CRO export tables carry no useful names

Patched Azahar to dump every CRO's named exports at load
(`XYORAS_DUMP_EXPORTS`), hoping module exports would identify the message
entry point. They do not:

```
# module DllLangSelect base 0X006A5000 exports 1
DllLangSelect   nnroControlObject_   0X006A6C1C
```

**One export per module, and it is the SDK's module control object.** Game
Freak exports no named functions, so the export table cannot lead us to
anything. Route closed.

Also learned: only `DllLangSelect` is loaded at the title screen. Reaching
`DllDialogCommon` or `DllField` requires progressing into the game, which needs
driving input the emulator does not expose to us. Any CRO work therefore starts
from static analysis of the module files in RomFS, not from a running game.

### Validating PK6 against the save: no Pokemon to validate against

`tools/inspect_save.cpp` runs the shipped PK6 code over a real save. Round-trip
tests prove self-consistency, but self-consistency is exactly what a subtly
wrong algorithm also has, so a real-data check was worth building.

The available save turns out to be from a freshly-started game:

- Party at `0x14200`: six empty slots.
- A scan of **every 4-byte-aligned offset** in the whole 0x65600 file found
  **zero** valid PK6 structures. A decrypted PK6 authenticates itself through
  its checksum, so this search does not depend on knowing the layout.

So the result is inconclusive rather than negative -- there is nothing in the
file to decrypt.

**What it did confirm:** the save is genuine and structurally as documented.
It is 51% non-zero, and the box-name block sits at `0x04400` with a stride of
exactly `0x22` bytes, matching PKHeX's `BoxLayout6`. Our reading of the save
format is correct; only the Pokemon are missing.

`inspect_save` is kept. It is the fastest way to validate PK6 against reality
the moment a save with Pokemon exists, and it needs no console.

**Standing gap:** PK6 remains verified by round-trip and by conformance to
PKHeX's documented algorithm, not against real encrypted data. Worth closing
when possible.
