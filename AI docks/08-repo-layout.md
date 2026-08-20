# 08 — Repo layout

```
xyoras-access/
├── CLAUDE.md                 Entry point for Claude Code; map to everything else
├── README.md                 Human-facing project introduction
├── LICENSE                   GPL-3.0
├── .gitignore                Build output, third-party sources, generated data
├── .gitattributes            LF normalisation
│
├── AI docks/                 Design + research documentation (this folder)
│   ├── README.md             Index
│   └── NN-topic.md           One file per concern
│
├── docs/                     User-facing documentation
│   ├── install.md            How a player installs the mod
│   ├── controls.md           Hotkey reference
│   └── troubleshooting.md    Common problems
│
├── plugin/                   The .3gx plugin — the actual product
│   ├── Makefile              devkitARM build + 3gxtool link step
│   ├── 3gx.ld                Linker script for the 3GX loader
│   ├── XYORASAccess.plgInfo  Plugin metadata (name, version, MemorySize)
│   ├── include/xyoras/       Public headers, mirroring source/ layout
│   ├── source/
│   │   ├── main.cpp          Lifecycle entry points
│   │   ├── config.cpp        Settings
│   │   ├── game/             Reading the game (no speech here)
│   │   ├── speech/           Saying things (no game reads here)
│   │   ├── features/         Deciding what to say (the only layer that does)
│   │   └── data/             Generated name tables
│   └── data/                 Binary assets compiled into the plugin
│
├── cmake/
│   └── 3DSToolchain.cmake    Cross-compile toolchain for dependencies
│
├── scripts/
│   ├── env.sh                Sets DEVKITPRO/DEVKITARM/PATH; sourced by others
│   ├── bootstrap.sh          One-time dependency setup
│   ├── build-espeak-3ds.sh   Cross-compile libespeak-ng.a
│   ├── build-plugin.sh       Build the .3gx
│   ├── package.sh            Produce dist/luma.zip
│   └── clean.sh              Remove build output
│
├── tools/                    Host-side helpers (Python/C#)
│   └── gen_name_tables.py    Turn name data into compiled C++ tables
│
├── third_party/              Fetched, never committed (see .gitignore)
│   ├── espeak-ng/
│   ├── libcwav/
│   └── libncsnd/
│
└── dist/                     Build artefacts, never committed
```

## Where things belong

| If you are adding... | Put it in |
| --- | --- |
| A new spoken feature | `plugin/source/features/` |
| A memory offset | `plugin/source/game/addresses.cpp` — nowhere else |
| A way to read a game structure | `plugin/source/game/` |
| Anything that produces sound | `plugin/source/speech/` |
| The exact wording of an utterance | `plugin/source/speech/phrases.cpp` |
| A design decision or a finding | the matching `AI docks/` file |
| A source URL or dead end | `AI docks/12-research-log.md` |
| Instructions for a player | `docs/` |
| A build or packaging step | `scripts/` |
| A tool that runs on the PC, not the 3DS | `tools/` |

## Naming conventions

- Directories and files: `lower_snake_case.cpp` / `.hpp`.
- Documentation: `NN-kebab-case.md`, numbered so the reading order is obvious.
- Scripts: `kebab-case.sh`, executable, `#!/usr/bin/env bash`, `set -euo pipefail`.
- The plugin binary and its `.plgInfo` are `XYORASAccess` (no spaces — some SD
  card tooling and the 3GX loader are happier without them).

## Things that must never be committed

See `14-legal-and-licensing.md` for the full list, but briefly: no ROMs, no
`code.bin`, no RomFS/ExeFS dumps, no extracted GARC contents, no ripped game
text or audio, no save files containing personal data, and no build output.
