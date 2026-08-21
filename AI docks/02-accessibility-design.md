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

### Where "cancels lower" and "never dropped" collide

`CRITICAL` cancels lower priorities, and `DIALOGUE` is lower than `CRITICAL`.
Taken literally those two rules mean a fainting message deletes the story line
queued behind it. **Dialogue wins.**

- `CRITICAL` clears pending `UI` and `AMBIENT`, and interrupts whatever is
  being spoken — so it is still heard immediately.
- `CRITICAL` does **not** clear pending `DIALOGUE`. The priority sort already
  places it ahead, so the critical message is spoken first and the story
  resumes after it. Nothing is lost by keeping the line.
- `INTERRUPT` **does** clear everything, dialogue included. The distinction is
  consent: the player asked for that, and message history can recover what was
  dropped. `CRITICAL` fires automatically, so it must not make that trade on
  the player's behalf.

This was found by the host tests, not by reading the spec — see
`12-research-log.md`.

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

## Wording decisions already made

All wording lives in `plugin/include/xyoras/phrases.hpp` and is pinned by
`tools/host-test/test_phrases.cpp`, so changing what the player hears requires
changing a test -- deliberately.

| Decision | Why |
| --- | --- |
| One HP left reads as "1 percent", never "0" | "0 percent" tells the player they fainted when they did not |
| 154 of 155 reads as "99 percent", never "100" | Rounding up hides chip damage |
| A fainted Pokemon says "fainted", not "0 of 155" | The fact, not a number to interpret |
| A fainted Pokemon's status is not mentioned | It no longer matters |
| Own HP exact, opponent's as a percentage | A percentage is all a sighted player is shown; exact values would breach rule 2 |
| A nickname alone when terse; nickname then species above that | The player chose the nickname, but an unfamiliar one still needs identifying |
| A null or empty menu entry says "blank" | Silence reads as the mod having broken |
| An empty tile ahead says "clear" | Same reason |
| The facing scan is terse by default | It is spoken constantly; every extra word costs time |
| Coordinates are verbose-only | Speaking them by default is noise |

## Reading the game's own text

Gen 6 draws every piece of text through `nw::lyt::TextBox`, and each instance
points at its UTF-16 string. So dialogue, menus and labels are all readable by
the same mechanism, with no render hook -- see `12-research-log.md`.

One consequence shapes the whole design: **message boxes type text out one
character at a time.** A poll mid-animation sees "Y", then "Yo", then "You".
Speaking each change would stutter the opening syllable of every line dozens of
times, and the mod would be unusable.

So text is spoken only once it has stopped changing (`screentext.hpp`). While
the animation runs the string keeps differing and the settle counter resets;
when it stops, the finished line is spoken once, whole.

Related rules that follow from the same place:

| Rule | Why |
| --- | --- |
| Text leaving the screen clears the memory of it | Reading the same sign twice should speak it twice |
| Text that never leaves is not re-spoken | Otherwise a static label repeats forever |
| Each source of text has its own tracker | A busy menu must not suppress a story line |
| An explicit reset on context change | Entering a battle must not be silenced by identical earlier text |

## Anti-patterns to avoid

- Speaking the same text twice because two subsystems both noticed a change.
  State-change detection is centralised for this reason.
- Reading raw internal identifiers ("map 132") when a name exists.
- Long unbroken utterances with no way to skip.
- Blocking the game to finish an utterance.
- Speaking coordinates constantly. Coordinates are on request only.
