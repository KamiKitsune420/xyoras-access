# 10 — Testing and QA

## The awkward truth

The plugin cannot be tested in an emulator, and this was confirmed by trying
(see `12-research-log.md`). Azahar *does* load a `.3gx` and parse its metadata,
which looks promising — but CTRPluginFramework then calls `svcCustomBackdoor`,
Luma3DS's custom SVC 0x80, which Azahar does not implement, and the plugin
stops there. Even past that point, Azahar's CSND is entirely stubbed, so
nothing would be audible.

The `CustomBackdoor` gap turned out **not** to be fatal — with checkpoint
instrumentation the plugin demonstrably runs under Azahar, including CTRPF,
eSpeak, and file access. What the emulator genuinely cannot do is **play
audio**, because CSND is stubbed, and it cannot be trusted for **timing**.

So Azahar covers more than expected: use it to exercise startup, file access,
synthesis, and the WAV backend. Only playback and performance **must** happen
on real hardware. This shapes the whole QA
approach: keep as much logic as possible testable off-target, and make
on-target testing cheap to repeat.

## Three test layers

### 1. Host unit tests (fast, run on the PC)

Pure logic with no 3DS dependency compiles and runs natively:

- PK6 decryption, block unshuffling, and field extraction, checked against
  known `.pk6` files.
- Phrase construction — given a structured description, does
  `phrases.cpp` produce the expected sentence at each verbosity level?
- Speech queue policy — priority ordering, interruption, `UI` coalescing,
  `DIALOGUE` never dropped.
- Name table lookups and bounds.

These are the tests that catch most regressions, so push logic down into
testable pure functions wherever possible.

```bash
scripts/host-test.sh
```

Finds a host compiler by itself — MSVC via `vswhere` (Visual Studio does not
need to be on `PATH`), otherwise `g++` or `clang++`. No host C compiler is
needed for the plugin build itself; this is the only thing that wants one.

The tests compile the **shipped** sources, not copies. `plugin/include/xyoras/sync.hpp`
is what makes that possible: it maps `Mutex`, `Lock`, and `WakeEvent` onto
CTRPF's primitives on the 3DS and onto `std::mutex` and friends on the host, so
`queue.cpp` builds unchanged either way. Test a copy of the code and you have
tested nothing.

Current coverage:

| Suite | Covers |
| --- | --- |
| `test_queue` | Priority ordering, interruption, `UI` coalescing, `AMBIENT` yielding, overflow behaviour, and that `DIALOGUE` is never dropped |
| `test_wav` | RIFF/WAVE header fields, sizes, struct layout, and the on-disk byte image |

Adding a suite: drop `test_<name>.cpp` in `tools/host-test/`, add `<name>` to
`TESTS` in `scripts/host-test.sh`, and set `SRCS_test_<name>` to any plugin
sources it needs compiled alongside it.

### 2. Emulator research (fast, not a test)

Azahar is used to **find and verify offsets**, not to test the plugin:

- Load a save, inspect memory, confirm an address holds what we think.
- Watch a value change while performing an in-game action.
- Confirm a structure layout before writing the reader.

Findings from here are marked `UNVERIFIED` until they are confirmed on
hardware, because emulator memory layout can differ from a real console.

### 3. Hardware testing (slow, authoritative)

Everything else. Keep a checklist and run it before every release.

## Hardware test matrix

| Axis | Values |
| --- | --- |
| Console | New 3DS, Old 3DS |
| Game | X, Y, Omega Ruby, Alpha Sapphire |
| Version | Latest update, and base if claimed supported |
| Save state | New game, mid-game, post-game |

Old 3DS is the performance-critical target — if synthesis latency is going to
fail anywhere, it fails there.

## First boot on hardware

The release checklist below assumes the mod works. This section is for the
first time it ever runs on a console, when it might not.

Order matters here. Each step depends on the one before it, so the first thing
that fails tells you where the problem is. Running them out of order turns one
clear answer into several ambiguous ones.

**Before powering on**, put these on the SD card:

```
SD:/luma/plugins/<TitleID>/XYORASAccess.3gx     (from dist/luma.zip)
SD:/xyoras-access/                              (eSpeak voice data, from the zip)
SD:/xyoras-access/self-test                     (empty file — asks for a report)
SD:/xyoras-access/trace-narration               (empty file — logs what it reads)
```

Do **not** create `dump-audio` yet. It diverts speech to .wav files, and
whether CSND actually plays over a running game is the single biggest thing
hardware has to answer. Adding it would skip that question.

### The sequence

| # | Question | How you know | If it fails |
| --- | --- | --- | --- |
| 1 | Does the plugin load at all? | `SD:/xyoras-access/checkpoints.txt` exists | Plugin loader off in Rosalina, or wrong title-ID folder |
| 2 | Does SD access work in a game process? | `checkpoints.txt` says `sdmc mounted` and `fopen WORKS` | This is what broke under emulation; see `platform.cpp` |
| 3 | Does eSpeak start? | `checkpoints.txt` says `espeak: ready` | Voice data missing or in the wrong place. eSpeak **hangs** rather than failing without it |
| 4 | **Does CSND play over the game?** | You hear the startup banner | Never tested anywhere. See below |
| 5 | Is narration allowed to run? | `checkpoints.txt` says `narration started` | See "your cart may be updated" below |
| 6 | Does the scan find panes? | `narration.txt` has `scan: N panes found` | N in the tens is right; 0 forever means the vtable address is wrong for this build |
| 7 | Is the scan fast enough? | The `in NNN ms` on each scan line | The 551 ms / 31 ms figures are emulated and prove nothing about hardware |
| 8 | Does it read the right text? | `narration.txt` `baseline:` block | Compare it against what is actually on screen |
| 9 | Does the read-screen tap work? | Tap ZL (or L+R together) | `narration.txt` logs `read screen:` |

**If the game hangs or the console freezes:** power off, take the SD card out,
and read `checkpoints.txt`. The last line is where it stopped. That file is
written through CTRPF's own file API before anything else can fail, and it
exists precisely because a blind player cannot read an exception screen.

### Step 4 is the real unknown

Everything up to synthesis has been proven — under emulation, but proven, and
the PCM was verified byte-for-byte against what CSND would have been handed.
What has never run anywhere is the last hop: CSND actually mixing that buffer
into a game that is already using the audio hardware.

If the banner is silent but `checkpoints.txt` says `speech started`, synthesis
worked and playback did not. Add `SD:/xyoras-access/dump-audio`, boot again,
and listen to the .wav files in `SD:/xyoras-access/speech/` on a computer. If
they sound correct, the problem is CSND alone and nothing upstream.

### Your cart may be updated, and then narration will refuse to start

`LayoutText` is verified for **XY version 0 only** — a base cartridge with no
update installed. An update replaces `code.bin`, which moves every address the
scan depends on.

So if the console has ever been online with the game in it, narration will
likely refuse to start and say so. **That is correct behaviour, not a bug**:
scanning for a vtable address from a different build finds nothing at best and
the wrong objects at worst.

The CTRPF menu's Status entry shows the detected update version. If it is not
0, that number is the first thing to write down — the vtable addresses have to
be re-derived for that build before narration can run on it, and the method is
in `04-gen6-reverse-engineering.md`.

### What to bring back

`checkpoints.txt`, `diagnostics.txt` and `narration.txt` together answer almost
every question above. Copy all three off the card before changing anything.

## Release checklist

**Startup**

- [ ] Plugin loads in all four games; startup banner is spoken.
- [ ] Correct game and series detected (check the CTRPF menu readout).
- [ ] An untested game version produces a spoken warning, not garbage.
- [ ] eSpeak data missing from SD produces a clear failure, not a crash.

**Speech**

- [ ] Speech is audible over game audio without distorting it.
- [ ] Speech rate, pitch, and volume settings take effect.
- [ ] Interruption is immediate; the stop hotkey silences instantly.
- [ ] Rapid menu movement does not cause a speech backlog.
- [ ] Dialogue is never dropped even under load.

**Audio system integration**

- [ ] Closing the lid (sleep) and reopening leaves audio working.
- [ ] HOME menu suspend and resume leaves audio working.
- [ ] Heavy game audio (battle music plus effects) does not starve speech.
- [ ] `NO_CHANNEL_AVAILABLE` is handled without a spin or a hang.

**Features**

- [ ] Position report matches what is on screen.
- [ ] Facing scan correctly identifies NPC, door, sign, wall, water, ledge.
- [ ] Map change announcement fires once, on the correct boundary.
- [ ] Battle narration covers start, each turn, and the end.
- [ ] Party and bag readouts match the game's own screens.
- [ ] Every hotkey chord works and does not collide with a game input.

**Stability**

- [ ] Two hours of continuous play with no crash and no memory growth.
- [ ] Save and reload mid-session; state tracking recovers.
- [ ] Game reset with the plugin loaded does not hang.

## Performance measurement

Track and record these in `12-research-log.md` per console:

| Metric | Target |
| --- | --- |
| Event to first audible sample | < 120 ms |
| eSpeak synthesis, short phrase | < 80 ms |
| Frame callback cost | < 1 ms |
| Hook callback cost | < 50 µs |
| Peak plugin heap use | under declared `MemorySize` |
| Frame rate impact on the game | none perceptible |

The frame callback and hook callbacks are the ones that can visibly hurt the
game, so measure them explicitly rather than assuming.

## Testing with blind users

This is the test that actually matters and it cannot be substituted with any
of the above.

- Recruit testers who use screen readers daily; their expectations for speech
  pacing, terseness, and interruption are the specification.
- Test with the screen off or covered — sighted developers unconsciously
  compensate with visual information.
- Ask "could you do this without help?" not "did it speak?".
- Watch for the two classic failures: **too much speech** (unplayable noise)
  and **missing state** (player stuck with no way to find out why).
- Log every "I got stuck here" moment. Those are the real bug reports.

## Debugging on hardware

- CTRPF's `OSD` can draw text on screen — useful for a sighted developer
  watching what the plugin thinks is happening.
- Write a log file to SD (`sd:/xyoras-access/log.txt`) behind a config flag;
  keep it off by default, since SD writes are slow.
- CTRPF ships an exception handler that reports crash details; capture and
  record the fault address, then map it back through the `.map` file produced
  by the linker.
- Keep the `.elf` and `.map` for every build you test, or a crash address is
  meaningless.
