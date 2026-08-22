# Azahar Lua input scripting

A patch against the [Azahar](https://github.com/azahar-emu/azahar) 3DS emulator
that lets a Lua script drive the controller and the touch screen. Applies to
upstream commit `8dcf732`, on top of the CSND tap in `../azahar-csnd-patch/`.

## Why this exists

Reverse engineering a game's dialogue means getting the game to an actual
dialogue box, and the emulator offers no way to script input.

The first attempt at this was an environment variable naming one button, which
the emulator pressed on a slow cycle. That is enough to walk through a screen
that only wants acknowledging, and it is not enough for anything else. Pokémon
X's opening asks which language, then asks to confirm it, then wants a
character chosen and **a name typed on the touch screen**. A single repeated
button cannot get through any of that, so every unattended run stopped in the
same place — and every measurement taken of the mod was taken on a screen that
has no story text on it at all.

## What it adds

A Lua script named by `XYORAS_LUA` runs as a coroutine, resumed once per pad
update. Anything that takes time yields, so the game runs normally in between
and the script reads as a straight sequence of actions.

```lua
press("a")                    -- press and release, with the gap a menu needs
chord({"l", "r"}, 12)         -- hold several at once: the mod's modifier
tap(160, 120)                 -- touch the bottom screen
seconds(2)                    -- wait, in real time rather than frames
log("got past the title")     -- to XYORAS_LUA_LOG and the emulator log
readfile(path)                -- read what the guest wrote, and react to it
```

`readfile` is the interesting one. The mod writes everything it can see to
`/xyoras-access/narration.txt`, so a script can ask the mod what is on screen
rather than guessing at timings — and the mod's own read-screen command
(`chord({"l", "r"})`) makes it write a fresh list on demand. The observer and
the thing being observed are the same program, which is convenient and only
mildly circular.

Buttons: `a b x y l r start select up down left right`.

`zl` and `zr` are accepted by name and then ignored. They are New 3DS buttons
carried by the IR service, not the pad, and `PadState` has no field for them.
Use `l`+`r` for the mod's modifier, which is exactly why it accepts that as an
alternative.

## Two things worth knowing

**Only the C primitives are in C.** `hold`, `release`, `wait`, `touch`, `log`
and `readfile` are implemented in `xyoras_script.cpp`; `press`, `chord`, `tap`
and `seconds` are defined in a Lua prelude inside that file. Timing a
press-and-release correctly in C is more code and no clearer.

**A finished script lets go of everything.** If it ends or throws, the held
buttons and any touch are cleared. Otherwise a script that dies mid-chord
leaves the game with a button stuck down, and the failure looks like the game
hanging rather than the script erroring.

## Applying

```bash
cd /path/to/azahar
git apply /path/to/tools/azahar-lua-patch/azahar-lua-input.patch
cp /path/to/tools/azahar-lua-patch/xyoras_script.* src/core/
```

Then vendor Lua 5.4 into `externals/lua/` — the `.c` and `.h` files from the
release tarball's `src/`, minus `lua.c` and `luac.c`, plus the `CMakeLists.txt`
this patch expects. Lua is MIT licensed and Azahar is GPL-2.0-or-later, which
are compatible.

The `hid.cpp` hunk in the patch is cumulative: it carries the earlier
`XYORAS_AUTO_PRESS`, vptr-scan and object-dump hooks alongside the scripting
one.

## Running

```bash
XYORAS_LUA=tools/lua/advance.lua \
XYORAS_LUA_LOG=/tmp/lua.log \
    azahar "Pokemon X.3ds"
```

Scripts live in [`tools/lua/`](../lua/).

## A caution learned the hard way

Do not run `cmake` on the build directory from MSYS2 or Git Bash. It mangles
the MSVC compiler paths in `CMakeCache.txt` down to `C`, and the next configure
re-detects the toolchain and invalidates every object file — turning a
two-minute incremental build into a full rebuild of 2231 targets. Use the
`.bat` wrappers from `cmd`, which call `vcvars64.bat` first.
