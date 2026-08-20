# 07 — Build environment

## What is already installed on this machine

| Component | Location | Status |
| --- | --- | --- |
| devkitPro | `C:/devkitPro` (`/opt/devkitpro` inside MSYS2) | Installed |
| devkitARM | `C:/devkitPro/devkitARM` | Installed |
| libctru | `C:/devkitPro/libctru` | Installed |
| devkitPro MSYS2 + pacman | `C:/devkitPro/msys2` | Installed |
| CMake | `C:/devkitPro/msys2/usr/bin/cmake` | Installed |
| GNU make | `C:/devkitPro/msys2/usr/bin/make` | Installed |
| 3DS CMake toolchain | `C:/devkitPro/cmake/3DS.cmake` | Installed |
| Python 3.12 | user install | Installed |
| Git, gh CLI | on PATH | Installed |
| eSpeak NG source | `~/Documents/llm experiments/misk/wii project/espeak-ng` | Clean upstream clone |

The eSpeak NG tree is an unmodified clone of `github.com/espeak-ng/espeak-ng`
with two local additions for a previous Wii project (`cmake/WiiToolchain.cmake`
and `src/wii_audio.c`). Our 3DS toolchain file is modelled on that Wii one.

## What bootstrap installs

`scripts/bootstrap.sh` adds two pacman repositories and installs four packages.

**Repositories** (appended to `/etc/pacman.conf` inside devkitPro MSYS2):

```
[thepixellizeross-lib]
Server = https://thepixellizeross.gitlab.io/packages/any
SigLevel = Optional

[thepixellizeross-win]
Server = https://thepixellizeross.gitlab.io/packages/x86_64/win
SigLevel = Optional
```

**Packages:**

| Package | Repo | Version at time of writing | Purpose |
| --- | --- | --- | --- |
| `libctrpf` | thepixellizeross-lib | `0.8.0.r1444-1` | CTRPluginFramework (includes the Sound API) |
| `3gxtool` | thepixellizeross-win | `1.3-1` | Links `.elf` + `.plgInfo` into `.3gx` |
| `libcwav` | fetched from source | — | BCWAV playback over CSND |
| `libncsnd` | fetched from source | — | CSND backend used by libcwav |

`libctrpf 0.8.0.r1444` corresponds to the CTRPF `develop` branch, which is the
line that carries `CTRPluginFramework/Sound/Sound.hpp`. The older `master`
branch (last touched 2020) has no sound engine and must not be used — the whole
audio design depends on it.

`libcwav` and `libncsnd` are built from source with `make install` into
`$DEVKITPRO`, as their READMEs describe. They are not in any pacman repo.

## Toolchain facts

Target: **ARM11 / ARMv6k**, little-endian, hard-float.

```
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := $(ARCH) -Os -mword-relocations -fomit-frame-pointer \
            -ffunction-sections -fno-strict-aliasing -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
LDFLAGS  := -T 3gx.ld $(ARCH) -Os \
            -Wl,--gc-sections,--strip-discarded,--strip-debug
LIBS     := -lctrpf -lcwav -lncsnd -lespeak-ng -lctru
```

Notes on why each unusual flag is there:

- `-mtp=soft` — plugins run in a context where the hardware thread pointer is
  not ours to use.
- `-fno-rtti -fno-exceptions` — CTRPF is built without them; mixing would not
  link, and exceptions in an injected plugin are a bad idea anyway.
- `-mword-relocations` — required for the position-independent loading the 3GX
  loader performs.
- `--gc-sections` with `-ffunction-sections` — plugin memory is scarce; this
  drops everything unreferenced, which matters a lot when linking eSpeak.
- `3gx.ld` — the linker script that lays the plugin out where the loader
  expects it.

## Build steps

```bash
source scripts/env.sh          # DEVKITPRO, DEVKITARM, PATH
scripts/bootstrap.sh           # once: repos, packages, third-party sources
scripts/build-espeak-3ds.sh    # cross-compile libespeak-ng.a + trim data
scripts/build-plugin.sh        # compile + link + 3gxtool -> XYORASAccess.3gx
scripts/package.sh             # build a luma.zip laid out for the SD card
```

### `build-espeak-3ds.sh`

Configures eSpeak NG with `cmake/3DSToolchain.cmake` and the option set from
`06-tts-audio-pipeline.md`, builds only the `espeak-ng` static library target,
and copies the trimmed English voice data into `dist/sdcard/`.

Output: `third_party/espeak-ng/build-3ds/src/libespeak-ng/libespeak-ng.a`

### `build-plugin.sh`

Runs `make` in `plugin/`. The Makefile follows the standard devkitARM 3DS
pattern with a 3GX-specific link step: link to `.elf`, then

```
3gxtool -d <plugin>.elf <plugin>.plgInfo <plugin>.3gx
```

### `package.sh`

Produces `dist/luma.zip` containing:

```
luma/plugins/0004000000055D00/XYORASAccess.3gx   (X)
luma/plugins/0004000000055E00/XYORASAccess.3gx   (Y)
luma/plugins/000400000011C400/XYORASAccess.3gx   (Omega Ruby)
luma/plugins/000400000011C500/XYORASAccess.3gx   (Alpha Sapphire)
xyoras-access/espeak-ng-data/...                 (voice data)
xyoras-access/sounds/...                         (non-speech cues)
xyoras-access/config.txt                         (default settings)
```

The same `.3gx` is copied into all four title-ID folders; the plugin detects
which game it is running in at startup.

## Deployment to hardware

1. Luma3DS installed and up to date.
2. Enable the plugin loader: Rosalina (`L` + `Down` + `Select`) → Plugin
   Loader → enabled. Required on New 3DS.
3. Extract `luma.zip` to the SD card root, merging with the existing `luma`
   folder.
4. Exactly one `.3gx` per title-ID folder. A second plugin in the same folder
   is undefined behaviour.
5. Launch the game. A startup banner is spoken when the plugin has loaded.

## Hard constraints, learned the hard way

Three of these will waste an afternoon each if you do not know them. All are
handled by the scripts; they are documented because the error messages point
nowhere useful. Full detail in `12-research-log.md`.

### The project path must contain no spaces

devkitPro's `base_rules` and `3ds_rules` do not quote `$(CURDIR)`. A space in
the path splits it and the build fails with a truncated path in the error. The
project therefore lives at `.../GitHub/xyoras-access`, not `xyoras access`.

### Builds must run inside devkitPro's own MSYS2

devkitPro's MSYS2 and Git Bash link different `msys-2.0.dll` runtimes. Run
devkitPro's `make` from Git Bash and environment variables are not inherited,
`PATH` does not carry, and sub-makes spawn the wrong shell — which has no
`/opt/devkitpro` mount. The symptom is a Makefile insisting `DEVKITARM` is
unset while `env` plainly shows it.

`scripts/msys-guard.sh` re-executes each script inside the right shell
automatically, so either shell works as a starting point.

### `TMPDIR` must be set

Otherwise `sed` and `gcc` try to write temporaries to `C:\WINDOWS` and fail
with a permission error. The guard exports `TMPDIR=/tmp`.

## Two things the packages do not give you

**`libctrpf` does not ship `types.h`.** Its own headers `#include "types.h"`,
but the package omits it; plugin projects are expected to supply their own.
Ours lives at `plugin/include/types.h`, taken from CTRPF `develop` along with
`csvc.h`. The integer typedefs it defines (`u8`, `u32`, `s16`, ...) are in the
**global** namespace, not inside `CTRPluginFramework`.

**eSpeak links as three libraries.** `libespeak-ng.a` alone leaves hundreds of
undefined `ucd_*` symbols. `libucd.a` and `libspeechPlayer.a` are built
alongside it and must follow `-lespeak-ng` on the link line.

## Voice data comes from an installation, not the build

eSpeak's `phondata`, `phontab`, `phonindex`, and `en_dict` are generated by
**host** tools during a normal build. We cross-compile, so those tools never
run and the source tree yields only a stub.

The compiled data is therefore copied from an eSpeak NG **installation**, and
library and data must be the same version — which is why `bootstrap.sh` pins
the eSpeak source to a release tag (`1.52.0`, recorded in `.espeak-version`).
`build-espeak-3ds.sh` searches for a matching install and accepts
`ESPEAK_SYSTEM_DATA` as an override.

A useful consequence: **no host C compiler is needed anywhere in this build.**
Visual Studio, MinGW, and friends are all irrelevant — devkitARM does all of
it.

## Shell environments

Two shells are in play on Windows and they are not interchangeable:

- **devkitPro MSYS2** (`C:/devkitPro/msys2/msys2_shell.bat`) — required for
  `pacman`. Paths there are `/opt/devkitpro/...`.
- **Git Bash** — paths are `/c/devkitPro/...`.

`scripts/env.sh` detects which it is in and sets `DEVKITPRO` accordingly, and
`scripts/msys-guard.sh` moves execution into devkitPro's MSYS2 when a build
needs it. Either shell is a fine starting point.

## Emulator setup (optional but recommended)

**Azahar** is the maintained 3DS emulator (the merged successor to Citra, built
from PabloMK7's Citra fork and Lime3DS). It is useful for inspecting memory and
iterating on game-state logic quickly.

Caveat: Azahar has **no Lua/scripting API and no 3GX plugin support**, so the
plugin itself cannot be tested there. The emulator is a research instrument for
finding offsets and understanding structures, not a test target. All plugin
testing happens on hardware. See `10-testing-and-qa.md`.

## Reproducibility

- Pin `libctrpf` and `3gxtool` versions in `scripts/bootstrap.sh` and record
  the versions actually installed in `12-research-log.md` when they change.
- Pin the eSpeak NG commit that `bootstrap.sh` checks out.
- Never commit build output. `.gitignore` covers `Build/`, `build-*/`, `*.3gx`,
  `*.a`, `*.elf`, and `third_party/`.
