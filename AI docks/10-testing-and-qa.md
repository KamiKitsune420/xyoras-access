# 10 — Testing and QA

## The awkward truth

The plugin cannot be tested in an emulator. Azahar (and Citra before it) has no
3GX plugin support and no scripting API, so **every test of the actual plugin
happens on real hardware**. This shapes the whole QA approach: keep as much
logic as possible testable off-target, and make on-target testing cheap to
repeat.

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
