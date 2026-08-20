# 01 — Project overview

## The problem

Pokémon X, Y, Omega Ruby, and Alpha Sapphire are almost entirely visual. A
blind player gets music and sound effects, but no dialogue, no menu labels, no
battle text, no map information, and no way to know where they are standing.
Unlike the Game Boy and Game Boy Advance generations — which blind players can
play through emulator scripting (see `12-research-log.md`) — the 3DS games have
had no accessibility path at all.

## The goal

A blind player should be able to boot Pokémon X/Y/OR/AS on a 3DS and play it
start to finish without sighted assistance:

- Hear every line of dialogue and every battle message as it appears.
- Hear menus, cursor position, and what is currently selected.
- Know where they are: map name, coordinates, facing direction, what is in
  front of them, and what is nearby.
- Navigate reliably to doors, NPCs, items, and route exits.
- Read their party, boxes, bag, and Pokédex.

## Approach in one paragraph

A Luma3DS `.3gx` plugin is injected into the game process. It watches game
memory and hooks a small number of game functions to learn what the game is
doing. A speech layer turns those observations into English sentences, feeds
them to an on-console eSpeak NG synthesiser, and plays the resulting audio via
the CSND service so it mixes over the game's own sound. A hotkey layer lets the
player query state on demand ("where am I", "read my party", "repeat that").

## Why on-console rather than on-PC

Two architectures were considered (`12-research-log.md` has the reasoning):

- **On-console plugin (chosen).** Works on real hardware, which is how most
  people own these games. No PC required, no network, no latency from a
  round trip. Costs: harder to develop, ARM11 is slow, memory is tight.
- **Emulator + host screen reader.** Much easier to build and matches how the
  older-generation Pokémon accessibility scripts work. But it locks players to
  a PC and an emulator, and the emulator in question has no scripting API.

The on-console design does not preclude a future host-side bridge; the speech
layer is kept behind an interface so an alternative output backend can be
added later without touching the game-state code.

## Scope

**In scope**

- English speech output for all four Gen 6 titles.
- Overworld navigation aids, battle narration, menu narration, and data
  readouts (party, bag, box, Pokédex).
- Configurable speech rate, volume, verbosity, and hotkeys.

**Out of scope (for now)**

- Languages other than English. The architecture keeps strings in one place so
  translation is possible later, but game-text extraction is language-specific
  and doubles the reverse-engineering work.
- Generation 7 (Sun/Moon/USUM) and Generation 4/5. Those are separate targets;
  a sister project already covers Gen 7 cheat tooling and could be extended.
- Cheat or randomiser functionality. Explicitly a non-goal — see rule 2 in
  `../CLAUDE.md`.
- Touch-screen-only content that has no keyboard/button equivalent, until a
  button-driven substitute is designed.

## Non-goals worth stating explicitly

- **Not a cheat plugin.** No infinite money, no shiny forcing, no RNG abuse.
  The mod must not make the game easier than it is for a sighted player.
- **Not a save editor.** Reading save data to describe it is fine; writing it
  is not, except where writing is the only way to deliver an accessibility
  feature and the change is one the player could have made in-game.
- **Not a ROM hack.** Nothing is patched into the game's files. The plugin is
  a runtime overlay, so it works with unmodified legally-dumped games.

## Who this is for

Primary users are blind and severely low-vision players who use screen readers
daily and are comfortable with terse, fast speech. Design decisions default to
what an experienced screen-reader user expects: speech is interruptible, terse
by default, verbose on request, and never repeats itself unprompted.

Secondary users are sighted players who want audio descriptions, and testers.
