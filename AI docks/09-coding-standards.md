# 09 — Coding standards

## Language and dialect

- **C++11** (`-std=gnu++11`), **no RTTI**, **no exceptions**. CTRPF is built
  this way; matching it is not optional.
- The C++ standard library is available but expensive. `std::string` and
  `std::vector` are fine; anything that allocates in a hot path is not.
- No `new`/`delete` in per-frame or hook code. Allocate at init.

## The layering rule

```
features/  ->  game/  and  speech/
game/      ->  (nothing above)
speech/    ->  (nothing above)
```

- `game/` reads memory and returns data. It never speaks and never knows a
  sentence exists.
- `speech/` turns text into sound. It never reads game memory.
- `features/` is the only layer that decides *what to say*. It reads from
  `game/` and pushes to `speech/`.

If you find yourself calling `Speak()` from `game/`, the design has gone wrong.

## Memory access

Every read of game memory goes through the guarded helpers in
`game/memory.cpp`, never through a raw pointer dereference.

```cpp
// Good
u32 value;
if (!mem::Read32(addr, value))
    return Unknown();

// Bad — faults take the game down with them
u32 value = *(u32*)addr;
```

Pointer chains are walked with range checks at every step:

```cpp
// Good
u32 p;
if (!mem::ReadPtr(base, p))          // ReadPtr range-checks the result
    return Unknown();
if (!mem::Read16(p + kSpeciesOffset, species))
    return Unknown();
```

The valid heap window is `0x8000000` to `0x8DF0000`; `mem::ReadPtr` enforces it.

## Offsets

Address literals appear in exactly one file: `game/addresses.cpp`. Every entry
carries the game series and the update version it was verified against, and
whether it is verified at all.

```cpp
// Verified: XY v1.5, ORAS v1.4 — money, u32, save-backed
constexpr AddrPair kMoney { 0x8C82B90, 0x8C8B35C };

// UNVERIFIED — inherited from community research, not re-checked
constexpr AddrPair kPokedexBlock { 0x8C6AC26, 0x8C7232A };
```

Access is always through the dispatch helper, never by picking a member:

```cpp
u32 addr = Addr(kMoney);   // resolves XY vs ORAS from detected series
```

## Hook callbacks

Hook callbacks run on a game thread, in the middle of the game's own work.
They must:

- Do no allocation.
- Do no file IO.
- Take no lock that anything slow also takes.
- Return quickly — copy the data you need and push it to a queue.

```cpp
// Good — copy and get out
void OnMessageRendered(const char* text)
{
    g_messageRing.TryPush(text);   // fixed-size ring, non-blocking
}
```

## Error handling

There are no exceptions, so errors are return values and they are always
checked. The policy from `05-plugin-architecture.md` applies: degrade, never
crash.

- A failed read yields "unknown", not a guess.
- A failed subsystem disables its own features and leaves the rest running.
- Nothing retries in a tight loop.

## Speech text

The exact words the player hears live in `speech/phrases.cpp`. Feature code
builds a structured description; `phrases.cpp` turns it into English.

```cpp
// Good — feature code describes, phrases.cpp words it
Speak(Priority::UI, phrases::MenuItem(name, index, count));

// Bad — wording scattered through feature code
Speak(Priority::UI, name + ", " + std::to_string(index) + " of " + ...);
```

This keeps tone consistent, makes verbosity settings implementable in one
place, and leaves a single file to translate later.

## Naming

| Kind | Convention | Example |
| --- | --- | --- |
| Types | `PascalCase` | `SpeechQueue`, `PK6` |
| Functions | `PascalCase` | `ReadPartySlot` |
| Variables | `camelCase` | `currentMapId` |
| Members | `camelCase` with trailing `_` | `queue_`, `isRunning_` |
| Globals | `g_` prefix | `g_speaker` |
| Constants | `k` prefix | `kMaxUtteranceLength` |
| Namespaces | `lower_snake` | `xyoras::game` |
| Files | `lower_snake_case` | `synth_espeak.cpp` |

Everything lives under the `xyoras` namespace, nested inside CTRPF's
`CTRPluginFramework` namespace where the framework requires it.

## Comments

Comment *why*, not *what*. The two things that always deserve a comment:

1. **Anything derived from reverse engineering** — where the offset came from,
   what game and version, how it was confirmed.
2. **Anything that looks wrong but is deliberate** — a workaround for framework
   or hardware behaviour.

```cpp
// Battle slots are 0x1E4 apart, not sizeof(PK6): the game stores extra
// battle-only state after each entry. Confirmed by stepping slots 0-5 in
// a double battle on ORAS v1.4.
constexpr u32 kBattleSlotStride = 0x1E4;
```

## Threading

- eSpeak has global state and is **not** thread-safe. Every eSpeak call happens
  on the synthesis worker thread. No exceptions.
- Shared state between the game thread and the worker is guarded with CTRPF's
  `Mutex` and RAII `Lock`.
- The frame callback and hook callbacks never block on a mutex the worker holds
  during synthesis. Use a fixed-size ring buffer with a try-push instead.

## Formatting

- 4 spaces, no tabs.
- 100-column soft limit.
- Braces on their own line for functions and types; same line for control flow
  is acceptable. Match the file you are editing.
- One `#include` block for framework headers, one for project headers.

## Before you commit

- It builds clean with no new warnings.
- No address literal outside `addresses.cpp`.
- No allocation added to a hook callback or the frame callback.
- New user-facing behaviour is described in `02-accessibility-design.md`.
- New findings are logged in `12-research-log.md`.
