# Azahar CSND audio tap

A patch against the [Azahar](https://github.com/azahar-emu/azahar) 3DS emulator
that makes CSND audio observable. Applies to upstream commit `8dcf732`.

## Why this exists

No 3DS emulator implements CSND audio output. Azahar tracks the complete CSND
channel state — encoding, buffer address, size, sample rate, volume, loop mode —
and then does nothing with it:

```cpp
case CommandId::Start:
    // TODO: start/stop the sound
```

CSND is the **only** audio path available to 3GX game plugins, because the
running game owns the DSP. So an emulator without it cannot test any plugin
that speaks, plays cues, or makes any sound at all — including this project.

This does not implement mixing. It writes the samples a channel *would* play to
a `.wav` file, which is enough to verify that the guest handed CSND correct
audio at the correct rate, address, and volume.

## Two things worth knowing

**There are two TODOs, and the obvious one is the wrong one.** libcwav never
issues `CommandId::Start`. It uses `ConfigureChannel` (0x00E), the combined
configure-and-start command, whose `enable_playback` branch has its own
separate TODO. Patching only `Start` compiles, runs, and produces nothing.

**The `sample_rate` field is not Hz.** It is a timer divider; the hardware
derives the rate as `0x03FEC3FC / divider`. A channel playing 22050 Hz audio
reports `3039`. Writing that straight into a WAV header yields a file that
plays at a seventh of the intended speed and sounds broken while being
perfectly correct data.

## Applying

```bash
git clone --recurse-submodules https://github.com/azahar-emu/azahar
cd azahar && git checkout 8dcf732
git apply /path/to/azahar-csnd-tap.patch
```

Build normally. Qt is required — Azahar has no SDL frontend any more
(`citra_meta/main.cpp` has `#error "citra_meta is somehow building with no
frontend"`), but CMake downloads Qt itself when `USE_SYSTEM_QT=OFF`.

## Output

Each channel start writes `<user dir>/csnd_dump_N.wav` and logs:

```
CSND TAP: channel=8 paddr=0X22B48088 bytes=142946 samples=71473 rate=22050
          encoding=1 volL=16384 volR=16384 loop=2 -> ...csnd_dump_0.wav
```

## What it proved here

The audio XYORAS Access hands to CSND is **byte-for-byte identical** to what
eSpeak synthesised — 71473 of 71473 samples, with matching RMS, peak and voiced
ratio. Together with the physical address matching what the plugin reported
(`0x22B48088` from both sides independently), that verifies the whole chain:
PCM → BCWAV → linear allocation → cache flush → VA→PA → libcwav → CSND.

## Licence

Azahar is GPL-2.0-or-later; this patch is offered under the same terms.
