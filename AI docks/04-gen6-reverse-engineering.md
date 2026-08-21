# 04 — Gen 6 reverse engineering

Everything known about getting information out of the Gen 6 games. Offsets
marked `UNVERIFIED` have not been confirmed by this project on hardware.

## The two halves of a 3DS game

A 3DS title splits into:

- **ExeFS** — the executable. `code.bin` (often `.code`) is the ARM11 program,
  plus `banner`, `icon`, `logo`. This is what runs; RAM addresses and hook
  targets live here. There is **no decompilation project** for Gen 6, so all
  code understanding comes from disassembly.
- **RomFS** — the asset filesystem. Static game data: text, models, maps,
  encounter tables, Pokémon stats. Organised as `a/<x>/<y>/<z>` files, most of
  which are **GARC** archives.

For an accessibility mod, RomFS matters as a source of *names and text*
(species names, item names, map names, ability descriptions) and ExeFS/RAM
matters as the source of *live state*.

## RomFS and GARC

**GARC** ("GameFreak ARChive") is a container format — effectively a bundle of
sub-files with an index. Community tooling reads and rebuilds them.

Selected Gen 6 RomFS locations (community-documented, see `12-research-log.md`):

| Path | Contents | Series |
| --- | --- | --- |
| `a/0/0/8` | Pokémon models | ORAS |
| `a/0/2/1` | Trainer overworld sprites | ORAS |
| `a/1/3/3` | Trainer battle models | ORAS |
| `a/0/3/9` | Map models | ORAS |
| `a/0/1/4` | Map textures | ORAS |
| `a/0/7/4` | Game text (superseded by updates) | XY |
| `a/0/8/2` | Game text (the copy updates use) | XY |

The text-archive split matters: XY stores game text in **two** archives, and a
system update makes the game read the second one. Any text extraction must
account for which archive the running version actually uses.

Text inside these archives is stored in a GameFreak-specific message format
with its own string table and obfuscation; `pk3DS` implements the reader and is
the reference for the format.

### Do we even need RomFS?

Two ways to get names:

1. **Read from RomFS at runtime.** Accurate, always matches the player's game
   and language, no data shipped. Costs: must implement GARC and message-format
   parsing on-console, and file access from a plugin is slower.
2. **Ship our own English name tables.** Simple and fast; names are short
   factual strings. Costs: version drift, and it is our own data to maintain.

**Decision: start with (2) for species/item/move/ability names** — a compact
generated table compiled into the plugin — because it unblocks everything
else, and keep (1) as the path for *dynamic* text (dialogue) which we cannot
ship. Dialogue is read from RAM as the game renders it, not from RomFS.

## The game is 84 CRO modules

**Most Gen 6 logic is not in `code.bin`.** The executable loads 84 CRO modules
(dynamically-linked libraries), one per subsystem, on demand. Confirmed by
dumping the executable and reading its strings -- see `12-research-log.md`.

The ones that matter to us:

| Module | Subsystem |
| --- | --- |
| `DllDialogCommon.cro` | Common dialogue -- the likely message box |
| `DllField.cro` | Overworld |
| `DllBattle.cro` | Battles |
| `DllPokeList.cro` | Party list |
| `DllBag.cro` | Bag |
| `DllStatus.cro` | Pokemon summary |
| `DllBox.cro` | Storage |
| `DllZukan.cro` | Pokedex |
| `DllStartMenu.cro` | Start menu |
| `DllTownmap.cro` | Town map |
| `DllStrInput.cro` / `DllNumberInput.cro` | Text and number entry |
| `DllConfig.cro` | Options |
| `DllTitle.cro` / `DllIntro.cro` / `DllLangSelect.cro` | Boot sequence |

Plus nine `DllPss*` modules for the Player Search System and six more
`DllField*` modules for individual field events.

### What this means for hooking

CROs are **relocated at load time**, so their addresses are not constant across
boots. A hook into a CRO cannot use a fixed address; it has to be installed
after the module loads, at `module_base + offset`.

That needs a level of dispatch above the XY/ORAS split: find the loaded module,
read its base, then apply an offset that is constant *within* the module. The
module list and per-module offsets are stable; only the base moves.

It also explains why the inherited community offsets all cluster in two narrow
bands. Those are static allocations reachable from `code.bin` -- the subset a
fixed-address cheat table can express. Anything inside a CRO was out of reach
of that technique.

## RAM: the hard part

Gen 6 games allocate from **heaps**, not fixed addresses. Most interesting
structures sit behind pointers that move between boots. Community research
handles this by:

- Finding a **static pointer** in the game's data segment and dereferencing it.
- Finding a **static address** for structures the game places deterministically.
- Setting **breakpoints in a disassembler/debugger** to catch the code that
  touches a value, then reading the pointer chain out of the instruction.

### Known-good static addresses

These come from community Gen 6 plugins (GPL-3.0, credited in
`12-research-log.md`). They are `(XY, ORAS)` pairs — first value is XY, second
is ORAS. **All are `UNVERIFIED` by this project** and must be re-checked
against the target update version before use.

| What | XY | ORAS |
| --- | --- | --- |
| Battle Pokémon data base | `0x81FF744` | `0x81FEEC8` |
| Battle slot stride | `0x1E4` | `0x1E4` |
| Battle state pointer | `0x81FB170` | `0x81FB478` |
| Battle offsets | `0x81FB284`, `0x81FB624` | `0x81FB58C`, `0x81FB92C` |
| Trainer name / identity | `0x8C79C84` | `0x8C81388` |
| Trainer ID block | `0x8C79C43` | `0x8C81347` |
| Money | `0x8C82B90` | `0x8C8B35C` |
| Battle Points | `0x8C82B94` | `0x8C8B360` |
| Play time | `0x8C79C69` | `0x8C8136D` |
| Game language | `0x8C79CB8` | `0x8C813BC` |
| Item pocket | `0x8C6A6AC` | `0x8C71DC0` |
| Medicine pocket | `0x8C6A6E0` | `0x8C71DE8` |
| Box / storage base | `0x8C861C8` | `0x8C9E134` |
| Pokédex block | `0x8C6AC26` | `0x8C7232A` |

Observations that generalise:

- The save-backed "trainer block" lives around `0x8C79Cxx` (XY) and
  `0x8C813xx` (ORAS). Adjacent fields (name, ID, money, language, play time)
  cluster there — a good region to explore for anything save-derived.
- Battle-time Pokémon data sits far lower, around `0x81FFxxx` and `0x81FExxx`,
  with a `0x1E4`-byte stride per battle slot.
- Valid heap addresses observed in the wild fall in `0x8000000` to `0x8DF0000`.
  A range check against that window is a cheap sanity guard before
  dereferencing anything.

### What we still need to find

Highest value first, all currently unknown:

1. **The message-box render path.** The single most valuable hook in the
   project. Finding the function that receives a formatted string before it is
   drawn gives us all dialogue and all battle text at once, with the game's own
   substitutions (player name, Pokémon nicknames) already applied.
2. **Player overworld coordinates and map ID.** Needed for every navigation
   feature. Save-based coordinate fields are known to the save-editing
   community (PKHeX edits them), which gives a starting point for the
   corresponding live values.
3. **Menu cursor state.** Which menu is open, how many entries it has, and
   which index is focused.
4. **Collision and tile data for the current map.** Needed for facing scan and
   surroundings scan. Likely the hardest item; a fallback is to infer
   walkability by watching whether a movement attempt changed the coordinates.
5. **Party structure in RAM**, as opposed to the battle slots above.

## PK6: the Pokémon structure

A Gen 6 Pokémon is **232 bytes** stored (box) and **260 bytes** in party — the
extra 28 bytes hold battle and overworld values.

Encryption: the structure is encrypted with an LCRNG seeded by the
**encryption constant** at offset `0x00`, and the four 56-byte blocks are
shuffled in an order derived from that same constant. This replaced the Gen 4/5
scheme, which seeded from the PID and used 32-byte blocks. Decryption is
identical whether the Pokémon sits in a box or in RAM.

Block layout after unshuffling:

| Block | Offset | Contents |
| --- | --- | --- |
| Header | `0x00` | Encryption constant, sanity, checksum |
| A | `0x08` | Species, held item, TID/SID, EXP, ability, PID, nature, EVs, contest stats, ribbons |
| B | `0x40` | Nickname (26 bytes UTF-16), moves, PP, PP-ups, relearn moves, IV bitfield |
| C | `0x78` | Handler trainer name, geolocation history |
| D | `0xB0` | OT name, memories, met data, level, version |
| Party extension | `0xE8` | Status, level, current HP, max HP, battle stats |

A reference C struct exists in the community Gen 6 plugin and can be adapted
directly; PKHeX's `PokeCrypto.cs` is the authoritative implementation of the
shuffle and the LCRNG.

The IV bitfield at the end of block B packs six 5-bit IVs plus the "is egg" and
"is nicknamed" flags into a single `u32`.

## Methodology for finding new offsets

1. **Reproduce a value change.** Pick something you can move deliberately —
   money, HP, coordinates.
2. **Search memory** for the value with the CTRPF in-game memory searcher,
   change it in-game, search again for the new value. Repeat until one address
   remains.
3. **Check stability.** Reset the game and re-check. If the address moved, it
   is heap-allocated and you need the pointer instead.
4. **Find the pointer.** Dump memory and run a pointer searcher across several
   dumps, or set a breakpoint on the address and read the base register out of
   the faulting instruction.
5. **Disassemble around the access site** to learn the structure — neighbouring
   fields usually reveal themselves at once.
6. **Record it** in the address table with game, version, and how it was
   verified, plus a line in `12-research-log.md`.

## Tooling

| Tool | Use |
| --- | --- |
| **GodMode9** | Dump your own cartridge or title to ExeFS + RomFS on-console |
| **pk3DS** | Read and rebuild Gen 6 GARC archives and game text; format reference |
| **PKHeX** | Authoritative reference for PK6 layout, crypto, and save structure |
| **CTRPF memory searcher** | Live value search on-console, no PC needed |
| **CTR-Heap-Mapper** | Understand how the game's heaps are laid out |
| **IDA Pro / Ghidra** | Static disassembly of `code.bin`; breakpoint work |
| **Azahar** | Desktop 3DS emulator for fast iteration and memory inspection |

## Guard rails

- Every dereference is range-checked before use against `0x8000000` to
  `0x8DF0000`.
- Every read goes through `Process::Read*`, which returns failure rather than
  faulting, and every failure is handled.
- Offsets never appear as literals at a call site — only in the address table.
