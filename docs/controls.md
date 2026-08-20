# Controls

> Not yet implemented — this is the intended scheme, recorded so it can be
> reviewed before it is built. The authoritative version lives in
> [`AI docks/02-accessibility-design.md`](../AI%20docks/02-accessibility-design.md).

The games use nearly every button, so the mod puts its commands behind a
**held modifier**. Hold the modifier and press a key.

The modifier is `ZL` by default on New 3DS, and `L`+`R` on consoles without
`ZL`. Change it with `modifier` in `config.txt`.

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
