# 13 — Glossary

## Accessibility

| Term | Meaning |
| --- | --- |
| **A11y** | Numeronym for "accessibility" (a + 11 letters + y) |
| **Screen reader** | Software that speaks a UI aloud. NVDA and JAWS on Windows, VoiceOver on Apple, Orca on Linux |
| **TTS** | Text-to-speech; converting text into synthesised audio |
| **Tolk** | A Windows library that abstracts over multiple screen readers so a program can push text to whichever is running |
| **SAPI** | Microsoft Speech API, the Windows built-in TTS interface |
| **Formant synthesis** | Speech synthesis that models the resonances of the vocal tract rather than replaying recorded audio. Small and fast; sounds robotic. eSpeak uses this |
| **Concatenative synthesis** | Synthesis by splicing recorded speech fragments. Sounds natural, needs a large voice database |
| **Verbosity** | How much detail speech output includes. Screen-reader users typically want terse |
| **Utterance** | One unit of speech the mod produces |
| **Earcon** | A short non-speech sound used as an interface cue (our movement tick, blocked-step thud) |
| **Panning** | Placing a sound left or right in the stereo field, used here to convey direction |

## 3DS platform

| Term | Meaning |
| --- | --- |
| **ARM11** | The 3DS's main application processor. ARMv6k, 268 MHz on Old 3DS, 804 MHz on New 3DS |
| **ARM9** | The security processor, running the older firmware half |
| **CFW** | Custom firmware |
| **Luma3DS** | The standard 3DS custom firmware. Includes the 3GX plugin loader |
| **Rosalina** | Luma3DS's in-game menu (`L` + `Down` + `Select`), where the plugin loader is enabled |
| **Title ID** | The 16-hex-digit identifier for a title. Determines which folder a plugin loads from |
| **ExeFS** | The executable filesystem of a title: `code.bin`, `banner`, `icon`, `logo` |
| **RomFS** | The read-only asset filesystem of a title |
| **code.bin** | The ARM11 executable extracted from ExeFS. Also seen as `.code` |
| **CIA / CXI / 3DS** | Container formats for installable titles and cartridge dumps |
| **GodMode9** | On-console file manager used to dump one's own games |
| **NDSP** | The libctru interface to the DSP audio service. Used by normal applications |
| **CSND** | The audio service applets use; plays over a running or suspended application. **What we use** |
| **DSP firmware** | `dspfirm.cdc`, required on SD for homebrew to use the DSP path |
| **BCWAV / CWAV** | The 3DS sound-effect container. Fully loaded into linear RAM |
| **BCSTM** | The 3DS streamed-audio container, for long music |
| **Linear RAM** | A physically contiguous memory region that hardware such as the audio and GPU units can address directly |
| **APT** | The applet service. Sends suspend, sleep, and exit events that audio code must handle |
| **libctru** | The core 3DS homebrew C library |
| **devkitARM / devkitPro** | The ARM cross-compiler toolchain and the wider distribution around it |
| **Azahar** | The maintained 3DS emulator, successor to Citra (from PabloMK7's Citra fork plus Lime3DS). No plugin or scripting support |

## Plugins and modding

| Term | Meaning |
| --- | --- |
| **3GX** | "3DS Game eXtension"; the plugin format loaded into a game process by Luma3DS |
| **3gxtool** | Builds a `.3gx` from a linked `.elf` plus a `.plgInfo` |
| **plgInfo** | Plugin metadata file: name, author, version, compatibility, `MemorySize` |
| **CTRPF** | CTRPluginFramework; the runtime library plugins are built against |
| **NTR CFW** | Older custom firmware whose `.plg` format 3GX descends from |
| **Hook** | Replacing an instruction with a branch to your own code so you observe or alter a function |
| **Trampoline** | The stub that runs the instruction a hook overwrote and jumps back, so the original function still works |
| **AR / Action Replay code** | A cheat-code format CTRPF can interpret |
| **OSD** | On-screen display; CTRPF's text overlay |
| **Pointer chain** | A sequence of dereferences from a stable address to a moving structure |
| **Heap** | Dynamically allocated memory. Gen 6 puts most live data here, which is why addresses move |

## Pokémon-specific

| Term | Meaning |
| --- | --- |
| **Gen 6** | Generation 6: X, Y, Omega Ruby, Alpha Sapphire |
| **XY** | Pokémon X and Y (Kalos) |
| **ORAS** | Pokémon Omega Ruby and Alpha Sapphire (Hoenn) |
| **GARC** | "GameFreak ARChive"; the container format for RomFS assets |
| **PK6** | The Generation 6 Pokémon data structure. 232 bytes stored, 260 in party |
| **PID** | Personality value; a per-Pokémon random number driving gender, nature, and shininess in earlier generations |
| **Encryption constant** | The Gen 6+ value at offset 0 that seeds PK6 decryption and block shuffling |
| **Block shuffle** | The permutation of PK6's four 56-byte blocks, derived from the encryption constant |
| **IV / EV** | Individual Values (innate, 0–31) and Effort Values (trained, 0–252 per stat) |
| **TID / SID** | Trainer ID and Secret ID |
| **OT** | Original Trainer |
| **PKHeX** | The reference save editor; the authority on Pokémon data formats |
| **pk3DS** | Gen 6/7 ROM editor and randomiser; the reference for GARC and game text formats |
| **DexNav / AreaNav** | ORAS bottom-screen features; touch-only, so they need accessible substitutes |
| **PSS** | Player Search System; the Gen 6 bottom-screen online/social interface |

## This project

| Term | Meaning |
| --- | --- |
| **XYORAS Access** | This project. X + Y + OR + AS, made accessible |
| **Speech queue** | The priority queue mediating everything the mod says |
| **Priority class** | `INTERRUPT`, `CRITICAL`, `DIALOGUE`, `UI`, `AMBIENT` — see `02-accessibility-design.md` |
| **Facing scan** | Reporting what occupies the tile the player faces |
| **Surroundings scan** | Reporting the eight tiles around the player |
| **Address table** | `game/addresses.cpp`; the only file allowed to contain address literals |
| **Series dispatch** | Choosing the XY or ORAS address set at runtime |
| **UNVERIFIED** | Marks a fact this project has not confirmed on hardware |
