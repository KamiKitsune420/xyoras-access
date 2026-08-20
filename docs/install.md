# Installing XYORAS Access

> **This is early development software.** It does not yet make the games
> playable. See the [roadmap](../AI%20docks/11-roadmap.md) for where things
> stand.

## What you need

- A Nintendo 3DS, 2DS, New 3DS, or New 2DS XL with **Luma3DS** installed.
- Your own copy of Pokémon X, Y, Omega Ruby, or Alpha Sapphire, installed on
  the console or in the cartridge slot.
- An SD card and a way to read it.

This project ships no game content. You supply the game.

## Steps

1. **Update Luma3DS** to a recent version. The plugin loader has been part of
   official Luma3DS since July 2023, so a current install already has it.

2. **Enable the plugin loader.** Start any game, open Rosalina with
   `L` + `Down` + `Select`, choose **Plugin Loader**, and turn it on. On New
   3DS this step is required — plugins are ignored without it.

3. **Copy the files.** Extract `luma.zip` to the root of your SD card, merging
   with the `luma` folder that is already there. You should end up with:

   ```
   SD:/luma/plugins/0004000000055D00/XYORASAccess.3gx    Pokémon X
   SD:/luma/plugins/0004000000055E00/XYORASAccess.3gx    Pokémon Y
   SD:/luma/plugins/000400000011C400/XYORASAccess.3gx    Omega Ruby
   SD:/luma/plugins/000400000011C500/XYORASAccess.3gx    Alpha Sapphire
   SD:/xyoras-access/espeak-ng-data/...                  voice data
   SD:/xyoras-access/config.txt                          settings
   ```

   Only one `.3gx` file may be in each title-ID folder. Remove any other
   plugin from those folders.

4. **Start the game.** If the plugin loaded, it speaks a short banner naming
   itself and the game it detected. That banner is your confirmation — if you
   hear nothing, it did not load.

## Settings

`SD:/xyoras-access/config.txt` is a plain text file you can edit from a
computer. Every setting is also reachable from the in-game menu, which is
spoken aloud.

Speech rate is the one most people change first. It is in words per minute and
accepts 80 to 450; the default of 200 is deliberately conservative.

## If it does not speak

See [troubleshooting.md](troubleshooting.md).

## Removing it

Delete the four `.3gx` files and the `SD:/xyoras-access` folder. Nothing is
written to your game or your save, so there is nothing else to undo.
