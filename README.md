# XYORAS Access

A blindness and low-vision accessibility mod for the four Generation 6 Pokémon
games on Nintendo 3DS: **X**, **Y**, **Omega Ruby**, and **Alpha Sapphire**.

> **Status: early development.** Nothing is playable yet. The research and
> build environment are in place; the plugin is being built out from there.
> See [`AI docks/11-roadmap.md`](AI%20docks/11-roadmap.md).

## What it does

The Gen 6 Pokémon games are almost entirely visual — dialogue, menus, battle
text, and the map are all on screen with no audio equivalent. Blind players
have had no way in.

XYORAS Access is a Luma3DS plugin that runs inside the game and speaks it
aloud:

- **Dialogue and battle text** read as it appears.
- **Menus** with the focused item, its position, and the menu title.
- **Navigation** — map name, coordinates, facing direction, what is in front of
  you, what is around you, and where the nearby doors and NPCs are.
- **Your data** — party, bag, boxes, and Pokédex on demand.
- **Hotkeys** to repeat, stop, and query anything at any time.

Speech is synthesised on the console itself with eSpeak NG and played over the
game's own audio. No PC, no network, no emulator.

## How it works

```
Luma3DS loads a .3gx plugin into the running game
        │
        ├── reads game state from the game's own memory
        ├── hooks the game's text rendering to catch dialogue
        │
        ▼
   turns it into English sentences
        │
        ▼
   eSpeak NG synthesises PCM on-console
        │
        ▼
   played through CSND, mixed over the game's audio
```

The design is described in [`AI docks/`](AI%20docks/README.md) — one document
per concern, starting with
[the project overview](AI%20docks/01-project-overview.md).

## Requirements

- A Nintendo 3DS, 2DS, New 3DS, or New 2DS XL with **Luma3DS** installed.
- A legally owned copy of Pokémon X, Y, Omega Ruby, or Alpha Sapphire.
- An SD card.

New 3DS users must enable the plugin loader in Rosalina
(`L` + `Down` + `Select`).

This project ships no game content and does not help anyone obtain a game.

## Building

Requires devkitPro with devkitARM. From MSYS2 or Git Bash:

```bash
source scripts/env.sh
scripts/bootstrap.sh          # once — installs libctrpf, 3gxtool, libcwav, libncsnd
scripts/build-espeak-3ds.sh   # cross-compile the speech engine
scripts/build-plugin.sh       # build the .3gx
scripts/package.sh            # produce dist/luma.zip for the SD card
```

Full details in [`AI docks/07-build-environment.md`](AI%20docks/07-build-environment.md).

## Design principles

**Translation, not transformation.** The mod speaks information the game
already shows a sighted player. It is not a cheat tool: no RNG manipulation, no
stat editing, no revealing hidden data. Anything that would give an advantage a
sighted player does not have is out of scope by design.

## Licence

**GPL-3.0** — see [`LICENSE`](LICENSE). eSpeak NG is GPLv3 and is linked into
the plugin, so the combined work is GPLv3.

## Credits

Built on work by many people:

- **ThePixellizerOSS** and **Nanquitas** — CTRPluginFramework
- **PabloMK7** — libcwav and the 3GX plugin loader
- **mariohackandglitch** — libncsnd, cwavtool
- **eSpeak NG** authors — the speech engine
- **kwsch (Kurt)**, **SciresM**, **Kaphotics**, and Project Pokémon — PKHeX,
  pk3DS, and the Gen 6 format research this depends on
- **semaj14**, **AnalogMan151**, **biometrix76**, **dragonfyre173**,
  **Alexander Hartmann**, **JourneyOver** and contributors — the Gen 6 CTRPF
  plugin lineage, source of the PK6 struct and many verified offsets
- **nuive** and contributors — `pokemon-access`, the design precedent for
  accessible Pokémon

Full attribution in
[`AI docks/14-legal-and-licensing.md`](AI%20docks/14-legal-and-licensing.md).
