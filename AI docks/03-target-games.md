# 03 — Target games

## Title IDs

The plugin must be installed once per title ID it should load for. Luma3DS
looks for `sd:/luma/plugins/<TitleID>/<name>.3gx`.

| Game | Title ID | Series | Notes |
| --- | --- | --- | --- |
| Pokémon X | `0004000000055D00` | XY | Kalos |
| Pokémon Y | `0004000000055E00` | XY | Kalos |
| Pokémon Omega Ruby | `000400000011C400` | ORAS | Hoenn |
| Pokémon Alpha Sapphire | `000400000011C500` | ORAS | Hoenn |

These are the region-free/worldwide title IDs used by all regions of the
retail releases; the same IDs are used by the eShop versions.

## Two code bases, four games

Practically, there are **two** executables to reverse-engineer, not four:

- **XY** — X and Y share nearly all code. Differences between X and Y are
  almost entirely data (version-exclusive encounters, legendaries, some
  strings), so an address that works in X usually works in Y at the same
  offset.
- **ORAS** — Omega Ruby and Alpha Sapphire likewise share code with each
  other, but ORAS is a substantially different build from XY. Every address
  differs.

The codebase reflects this with a two-level dispatch:

```
GameSeries : XY | ORAS       // picks the address table
GameName   : X | Y | OR | AS // picks data-level differences only
```

Reference implementations of this pattern exist in the community Gen 6 cheat
plugins (`AutoGameSet(kalosValue, hoennValue)` and `AutoGame(first, second)`);
see `12-research-log.md`.

## Game update versions

Both titles received system updates that **moved code and data addresses**.
This is the single biggest source of "worked for me, crashed for them" bugs.

| Game | Base | Notable updates |
| --- | --- | --- |
| XY | v0 | v1.1, v1.2, v1.3, v1.4, v1.5 |
| ORAS | v0 | v1.1, v1.2, v1.3, v1.4 |

Rules for this repo:

1. Target the **latest** update version first (that is what most players have),
   and record which version each offset was verified against.
2. On startup, read the process version (`Process::GetVersion()`) and refuse to
   run — with a spoken message — if it is an untested version. A silent wrong
   read is worse than a clear refusal.
3. Any address table entry carries a comment naming game series **and**
   version.

## Regions and languages

The games are region-free at the title-ID level but the in-game language is
chosen at first boot and stored in the save. That matters because:

- Game text read out of memory is in the save's language.
- Our English output layer will mismatch a non-English save.

Startup should detect the game language and, if it is not English, announce
that only partial support is available rather than speaking garbled text.
Language detection offsets are noted in `04-gen6-reverse-engineering.md`.

## Hardware targets

| Target | Status | Notes |
| --- | --- | --- |
| New 3DS / New 2DS XL | **Primary** | 804 MHz, 256 MB RAM — comfortable headroom for on-console synthesis |
| Old 3DS / 2DS | **Secondary** | 268 MHz, 128 MB RAM — synthesis latency and plugin memory are the risks |

Both require Luma3DS with the plugin loader enabled (Rosalina → `L` + `Down` +
`Select`). On New 3DS the plugin loader must be explicitly enabled; see
`07-build-environment.md` for deployment.

Old-3DS viability is an open question until measured. If eSpeak synthesis is
too slow there, the fallbacks are (in order): lower sample rate, a smaller
voice/phoneme set, or a pre-rendered clip cache for the most common phrases.
Tracked in `11-roadmap.md`.

## Legally required of the player

The player supplies their own game. This project ships no game content and
does not distribute or enable piracy. See `14-legal-and-licensing.md`.
