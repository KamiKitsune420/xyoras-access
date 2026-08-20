# 02 — Accessibility design

This file defines *what the player hears and how they control it*. Any change
to user-facing behaviour should be reflected here first.

## Principles

1. **Translation, not transformation.** Speak information the game already
   presents visually. Never reveal what a sighted player cannot see.
2. **Terse by default, verbose on demand.** Default utterances are short.
   A modifier key or a second press gives detail.
3. **Interruptible.** Any new speech cancels the current utterance unless it is
   explicitly queued. The player must never be stuck listening.
4. **Never repeat unprompted.** State changes speak once. Re-reading is always
   an explicit request.
5. **Silence is information.** If nothing changed, say nothing. Constant
   chatter makes the game unplayable.
6. **The player is never lost.** There is always a hotkey that answers "where
   am I, what am I facing, what is in front of me".

## Speech categories and priority

Utterances are classified; the classification decides queueing and interruption.

| Priority | Category | Behaviour | Examples |
| --- | --- | --- | --- |
| 0 | `INTERRUPT` | Cancels everything, speaks immediately | Player-requested query, "cancelled" |
| 1 | `CRITICAL` | Cancels lower, must be heard | Battle prompt, "your Pokémon fainted" |
| 2 | `DIALOGUE` | Queued in order, never dropped | NPC dialogue, story text |
| 3 | `UI` | Replaces a pending same-category item | Menu cursor moved, selection changed |
| 4 | `AMBIENT` | Dropped if anything else is pending | Step counter, tile description |

Rule: a new `UI` utterance replaces an unspoken pending `UI` utterance rather
than queueing behind it, so fast cursor movement does not cause a speech
backlog. `DIALOGUE` never drops — losing a story line loses the game's plot.

## Feature set

### Overworld

- **Position report** — map name, X/Y coordinates, facing direction.
- **Facing scan** — what occupies the tile directly ahead: NPC, sign, door,
  ledge, water, wall, item ball, grass.
- **Surroundings scan** — the eight adjacent tiles, read clockwise from north,
  collapsing runs ("walls north through east").
- **Movement feedback** — a short non-speech tick per step; a distinct sound
  when a step is blocked, so the player can feel walls without speech spam.
- **Landmark list** — nearby doors, warps, NPCs, and item balls with direction
  and distance, sorted by distance.
- **Auto-announce on map change** — new map name spoken when crossing a border.

### Dialogue and text boxes

- Speak every message box as it appears, including choice prompts.
- Speak the list of choices and the current cursor position for yes/no and
  multiple-choice prompts.
- **Repeat last message** hotkey; **message history** (last N messages) with
  scrollback.

### Battle

- Announce battle start, opponent, and the opposing Pokémon's species, level,
  and gender.
- Speak every battle message (moves used, effectiveness, status, damage).
- Announce HP as both absolute and percentage for the player's active Pokémon;
  percentage only for the opponent (matching what the game shows).
- Speak the move menu: move name, type, PP remaining, and category on request.
- Speak switch menu entries: species, level, HP, status.
- **Battle status hotkey** — full read of both active Pokémon.

### Menus and lists

- Speak the focused item and its position ("Potion, 3 of 12").
- Speak the menu title on entry and on request.
- Speak quantity, description, and cost where the game shows them.

### Data readouts

- **Party** — species, nickname, level, HP, status, held item.
- **Pokémon detail** — stats, IVs/EVs where the game exposes them, moves, PP,
  ability, nature, held item, ribbons.
- **Bag** — pockets, items, quantities.
- **Box** — box name, slot occupancy, and per-slot Pokémon summary.
- **Pokédex** — seen/caught state and entry text.
- **Trainer card** — name, ID, money, play time, badges.

### Touch-screen substitutes

Gen 6 puts real functionality on the bottom screen (PSS, Super Training,
Player Search, the ORAS DexNav and AreaNav). Each needs a button-driven
equivalent or an explicit "not accessible yet" announcement rather than
silence. Tracked in `11-roadmap.md`.

## Control scheme

The 3DS has few free buttons and the game uses nearly all of them, so the mod
uses a **modifier-held layer**: hold `ZL` (New 3DS) or `L`+`R` (Old 3DS) and
press a key. The modifier is configurable.

| Chord | Action |
| --- | --- |
| Mod + `A` | Repeat last utterance |
| Mod + `B` | Stop speech |
| Mod + `X` | Position report (map, coordinates, facing) |
| Mod + `Y` | Facing scan (what is directly ahead) |
| Mod + `D-Pad Up` | Surroundings scan |
| Mod + `D-Pad Down` | Nearby landmarks |
| Mod + `D-Pad Left` / `Right` | Message history back / forward |
| Mod + `L` | Party summary |
| Mod + `R` | Battle status |
| Mod + `Start` | Open the mod's settings menu (CTRPF menu) |
| Mod + `Select` | Toggle verbosity (terse / verbose) |

While the CTRPF settings menu is open, the game is paused and the menu itself
is spoken, so the settings are reachable without sight.

## Settings

Persisted to SD card as a plain-text config the player can also edit from a PC.

- Speech rate (eSpeak words-per-minute), pitch, volume, voice variant.
- Verbosity: terse / normal / verbose.
- Per-category mute (e.g. silence `AMBIENT` step feedback).
- Movement tick sound on/off.
- Modifier key choice.
- Auto-announce map changes on/off.

## Anti-patterns to avoid

- Speaking the same text twice because two subsystems both noticed a change.
  State-change detection is centralised for this reason.
- Reading raw internal identifiers ("map 132") when a name exists.
- Long unbroken utterances with no way to skip.
- Blocking the game to finish an utterance.
- Speaking coordinates constantly. Coordinates are on request only.
