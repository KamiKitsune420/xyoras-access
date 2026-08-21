# Controls

> Mostly not implemented yet. The table below is the intended scheme; the
> **Working now** section says what actually responds today. The authoritative
> version lives in
> [`AI docks/02-accessibility-design.md`](../AI%20docks/02-accessibility-design.md).

The games use nearly every button, so the mod puts its commands behind a
**held modifier**. Hold the modifier and press a key.

The modifier is `ZL` by default on New 3DS, and `L`+`R` on consoles without
`ZL`. Change it with `modifier` in `config.txt`.

## Working now

| Chord | Action |
| --- | --- |
| **Tap the modifier alone** | **Read the whole screen** |
| Mod + `A` | Repeat the last thing spoken |
| Mod + `B` | Stop speaking |

Tapping the modifier means pressing and releasing it without pressing anything
else. It is the "say all" every screen reader has, and it costs no button —
there was not a spare one to give it.

Because of that, the modifier cannot act until you let go of it: until then
there is no way to tell whether another key is coming. That is a few frames,
and it applies to nothing else here.

Holding the modifier and pressing a key that is not listed above does nothing,
and also cancels the tap — so exploring the controls never reads the screen at
you unexpectedly.

## Speech control

| Chord | Action |
| --- | --- |
| Mod + `A` | Repeat the last thing spoken |
| Mod + `B` | Stop speaking |
| Mod + `Select` | Switch between terse and verbose |

## Where am I

| Chord | Action |
| --- | --- |
| Mod + `X` | Map name, coordinates, and facing direction |
| Mod + `Y` | What is directly in front of you |
| Mod + `D-Pad Up` | The eight tiles around you |
| Mod + `D-Pad Down` | Nearby doors, people, and items |

## Reading

| Chord | Action |
| --- | --- |
| Mod + `D-Pad Left` | Previous message |
| Mod + `D-Pad Right` | Next message |
| Mod + `L` | Party summary |
| Mod + `R` | Battle status, both active Pokémon |

## Settings

| Chord | Action |
| --- | --- |
| Mod + `Start` | Open the mod's menu |

The menu pauses the game and reads itself aloud, so every setting is reachable
without sight.

## Notes on the design

- Any new speech interrupts what is being said, except story dialogue, which
  is queued so no line is ever lost.
- Moving the cursor quickly through a menu will not build up a backlog: each
  new menu item replaces the one waiting to be spoken.
- Ordinary steps produce a short tick rather than speech, and a blocked step
  produces a different sound. Turn these off with `movement_cues = off` if you
  prefer silence.

## Diagnostics

| Chord | Action |
| --- | --- |
| Mod + `X` | Write a snapshot of the screen's layout to `narration.txt` |

This one is temporary and does **nothing** unless the file
`SD:/xyoras-access/trace-narration` exists. It is how the mod is being taught
to tell which item in a menu is selected: sit on a menu, take a snapshot, move
the cursor, take another. Whatever differs between them is the marker.

`Mod + X` becomes the position report once that exists.
