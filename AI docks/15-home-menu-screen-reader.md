# 15 — Home Menu screen reader

A second plugin target, `plugin-screenreader/`, aimed at a different problem
from the Gen 6 mod: reading **system UI** aloud — Home Menu and the system
applications around it — so a blind user can set up and navigate a console
without sighted help. It shares this repo because it needs exactly one thing
the Gen 6 work already solved: `plugin/source/speech`.

It reads no game memory, so the layering rule in `09-coding-standards.md` is
trivially satisfied: it is a `features/`-shaped consumer of `speech/` and
nothing else.

## The constraint that shapes everything

**A 3GX plugin can never load into the real Home Menu.**

Azahar's plugin loader (`src/core/hle/service/plgldr/plgldr.cpp`) gates loading
on the title id:

```
(title_id_high & 0xFFFFFFED) == 0x00040000
```

Home Menu is `0x00040030`. `0x30 & 0xED == 0x20`, so it fails. The only title
highs that pass are those whose low byte is `0x00`, `0x02`, `0x10`, or `0x12`:

| Title high | What it is | Pluggable |
| --- | --- | --- |
| `0x00040000` | Applications (games) | yes |
| `0x00040002` | Demos | yes |
| `0x00040010` | **System applications** (System Settings, Mii Maker, …) | yes |
| `0x00040030` | **System applets, incl. Home Menu** | **no** |

The comment in that file says the check mirrors the original Luma3DS loader, so
this very likely holds on hardware too — worth confirming against Luma3DS
source before relying on it.

**Consequence:** the eventual Home Menu reader must be a Luma3DS sysmodule or a
Home Menu patch, not a plugin. `plugin-screenreader/` is therefore not the
final delivery vehicle. It exists to build and prove the parts that transfer:
text capture by hooking, and the speech path. System *applications*
(`0x00040010`) remain reachable as a plugin, and are a worthwhile target in
their own right — System Settings is exactly what a new user cannot get past.

The loader also refuses devkitARM homebrew, sniffing the crt0 signature
(`B #0x20` / `MOV R4, LR`), and rejects plugins whose plgInfo says
`Compatibility: Console` (`plugin_3gx.cpp:96`). Hence `Compatibility: Any`.

## Development without a console

Home Menu cannot be booted in Azahar without system files from a physical 3DS,
streamed in by the Artic Setup Tool. `AreSystemTitlesInstalled()` gates it and
there is no offline path — Azahar's `nus_download.cpp` has no callers, and a
`keys.txt` supplies decryption keys, not content. (Azahar builds its own key
blob in by default via `ENABLE_BUILTIN_KEYBLOB`, so a keys file adds nothing.)

So the development target is `plugin-screenreader/testapp`, a homebrew host
that draws a Home-Menu-shaped list of items. Its `sr_draw_item(int, const char*)`
is the hook target — `noinline`, `used`, and non-static so the symbol survives
for `nm`. Nothing copyrighted is involved.

Two emulator-side changes are needed, both dev-only:

1. Azahar must be built from the `screenreader-dev` branch, which adds an
   `AZAHAR_PLUGIN_ANY_TITLE=1` env check that bypasses the title-id and
   homebrew gates. Unset, behaviour is unchanged.
2. The plugin loader must be enabled in Azahar's System settings.

## MEASURED: CTRPF needs a CTR-SDK process, so no synthetic host will work

A purpose-built test host does not work, and now the reason is known: **CTRPF
cannot run in a program built with devkitARM/libctru**, regardless of how that
program is packaged or what permissions it holds.

### Evidence

| Host | Built with | Known-good `XYORASAccess.3gx` |
| --- | --- | --- |
| `testapp.3dsx` | devkitARM/libctru | abort |
| `testapp.cxi`, `-desc app:4` | devkitARM/libctru | abort |
| `testapp.cxi`, full RSF (125 SVCs, 33 services) | devkitARM/libctru | abort |
| `testapp.cxi`, kernel caps **byte-identical to retail** | devkitARM/libctru | abort |
| Pokemon X | CTR-SDK | **works** |

The failure is always the same, and always before the plugin's `main()`:

```
Service.PLGLDR <Error> Plugin error - Title: Fatal plugin error
                       - Description: abort() called from the following location:
Debug.Emulated <Critical> Break reason: PANIC
```

Two controls make this conclusive. `testapp.cxi` **with no plugin runs fine**,
so the host is not broken. And the plugin that aborts is the *known-good* one
that runs in Pokemon X, so the plugin is not broken. The only variable left is
what the host was compiled with.

The ExHeader was eliminated by construction: the retail ExHeader was read out of
the (decrypted) Pokemon X NCSD and mirrored field by field until
`AccessControlInfo` kernel capabilities matched exactly --

```
F0FA9F4E F1FFBFFF F2003FE7 FF81FF50 FF81FF58 FF81FF70 FF81FF78
FF91F000 FF91F600 FF000101 FE000200 FC000223
```

-- same SVC masks, same hardware mappings, same kernel flags, same handle table,
same kernel release version, same 64MB system mode. It still aborts. So the
difference is in the program, not its permissions.

### Why the plgldr check is keyed the way it is

`plgldr.cpp` decides "is this homebrew" by reading the first instructions of the
code segment:

```
word[0] == 0xEA000006   (b #0x20)
word[8] == 0xE1A0400E   (mov r4, lr)
```

That is libctru's crt0. The check is not asking "is this a `.3dsx`" -- it is
asking **"was this built with libctru"**, which is exactly the thing that does
not work. `testapp.cxi` is a genuine NCCH application with a real title id and
retail permissions, and it still trips the check, because it is still a libctru
program:

```
100000:  ea000006   b  100020 <startup>
```

So `AZAHAR_PLUGIN_ANY_TITLE` overrides a check that was making a correct
prediction. It is still useful for experiments, but it cannot make a libctru
host viable.

### Consequence

**There is no synthetic host.** Building one would mean building against CTR-SDK,
which is not available. Develop the menu hook against a real game, where CTRPF
is proven to work in this exact emulator build.

This costs less than it appears. The eventual target is system UI, and that
cannot be reached by a plugin at all -- see the title-mask section above -- so
the test host was always scaffolding, never a step toward delivery. A game's
menus exercise the same `InitializeForMitm` -> capture -> `speech::Say` path.

## WORKING: standalone TTS homebrew (no CTRPF)

`plugin-screenreader/testapp` now speaks its menu aloud in Azahar, audibly, on a
machine with no 3DS. This is the route that works, and it is architecturally
different from the plugin.

```
menu item text -> eSpeak NG (linked in) -> PCM via synth callback
               -> linear buffer -> NDSP channel 0 -> speakers
```

**No CTRPluginFramework, no plugin injection, no host process to attach to.**
eSpeak is linked directly into the homebrew, which is what the existing TTS
homebrew on 3DS does. That sidesteps the CTRPF/CTR-SDK wall entirely, because
the wall was CTRPF's, not eSpeak's.

The second benefit is the one that matters for development: **a standalone app
owns the DSP, so it uses NDSP, which Azahar actually emulates.** A game plugin
must use CSND because the game already owns DSP, and no emulator renders CSND --
that is why `06-tts-audio-pipeline.md` has to divert plugin speech to `.wav`.
Standalone speech is simply audible.

### Four things it needs, all of which cost a day if unknown

1. **`sdmc:/3ds/dspfirm.cdc` must exist.** libctru refuses to initialise ndsp
   without it and returns a bare DSP result code (`D980877A`) that names no
   cause. Azahar HLEs the DSP so the contents are never read -- **an empty file
   is sufficient**, no console dump required. devkitPro's own
   `examples/3ds/audio/README.md` documents this. `run-screenreader.sh` now
   creates it automatically.

2. **`ndspSetMasterVol(1.0f)`.** It is not implicitly 1.0. Without it the
   channel plays into silence while every status indicator looks healthy.

3. **A much bigger stack.** eSpeak recurses deeply; libctru's default 32 KB main
   thread stack overflows a few seconds in, walking off into unmapped memory and
   ending at PC 0. `unsigned int __stacksize__ = 512 * 1024;`.

4. **`-lstdc++` on the link line.** `speechPlayer` contains C++, and the
   devkitPro template links with `gcc` when the project has no `.cpp` files.

### Draw before you initialise

`espeak_Initialize` hangs rather than failing when it cannot read its voice
data. If speech is brought up before the first frame is drawn, that hang is
indistinguishable from "the emulator never loaded anything" -- a blank window
with no error. The app now paints its menu and a status line first, so the
screen always says how far startup got.

### What this means for the project

The eventual target is system UI, which no plugin can reach (see the title mask
above). But an **accessible homebrew launcher with TTS built in** is achievable
today, needs no console, and addresses the actual problem: a blind user who
cannot get through initial setup unaided. That is a delivery vehicle, not
scaffolding.

## How the hook works

`Hook::InitializeForMitm` on the target address, with a callback matching the
target's signature so the arguments land in the right registers. The callback
speaks the string and then calls the original through the hook context, so the
host still draws normally.

The address is read at runtime from `sdmc:/screenreader.cfg` rather than
compiled in, because it changes on every testapp rebuild and, later, will come
from Ghidra. `scripts/build-screenreader.sh` regenerates that file from `nm`
after each build so a stale address is never left to debug.

## Speech

`Priority::Ui` — a newer item replaces a pending one rather than queueing — is
the correct class for menu focus. Holding the d-pad must not back up a queue of
stale item names. The queue already de-duplicates, so the hook does not.

`source/tts.cpp` is a thin adapter over `xyoras::speech` with an OSD fallback
used when the plugin is built without the speech stack. The fallback is not
just a stub: it means an audio failure looks different from a hook failure.
