# Troubleshooting

## Nothing is spoken at all

**Check the plugin loaded.** The mod speaks a banner at startup naming itself
and the game. No banner means the plugin never ran. In order of likelihood:

1. The plugin loader is off. Rosalina (`L` + `Down` + `Select`) → Plugin
   Loader → enable. This is the usual cause on New 3DS.
2. The `.3gx` is in the wrong folder. It must sit in a folder named for the
   title ID of the game you are playing — see [install.md](install.md).
3. There is more than one `.3gx` in that folder. Leave only ours.
4. Luma3DS is too old. Update it.

**Check the voice data is present.** `SD:/xyoras-access/espeak-ng-data/` must
contain `phondata`, `phontab`, `phonindex`, and `en_dict`. Without them the
plugin loads but cannot speak. Open the mod's menu and read the **Status**
entry: it reports whether speech started.

## It speaks the banner, then nothing else

Expected at this stage of development. Reading game state is not implemented
yet — see the [roadmap](../AI%20docks/11-roadmap.md).

If the banner included a warning that your game version has not been tested,
that is doing its job: features that read game data stay disabled rather than
guess. Report your game and update version so it can be added.

## Speech is cut off, or stops after closing the lid

Close and reopen the lid once, or press the stop-speech hotkey. If it stays
broken, that is a bug in the audio system's sleep handling — please report it
with what you were doing when it happened.

## Speech is too slow, or too fast

Not adjustable yet — the rate is compiled in at 200 words per minute.
Valid range is 80 to 450 words per minute.

## The game crashes

Note the error details if anyone sighted can read them, and report:

- Which game, and its update version.
- Which console (Old 3DS or New 3DS).
- What you were doing.
- The version of XYORAS Access.

A plugin crash takes the game down with it, so these reports matter a great
deal. To get playing again, remove the `.3gx` files; the game runs normally
without them.

## The game runs slowly

Report it. The mod is meant to be imperceptible in the game's frame rate, and
anything else is a bug. Old 3DS is the tighter target.

## Reporting a bug

Include the game, update version, console model, mod version, and what you
were doing. If you can, create an empty file `SD:/xyoras-access/trace-narration`,
reproduce the
problem, and attach `SD:/xyoras-access/log.txt`. Turn it back off afterwards —
logging writes to the SD card and slows things down.
