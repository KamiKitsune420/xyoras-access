// XYORAS Access — Lua input scripting for the emulator.
// Licensed under GPLv2 or any later version, as the rest of Azahar.
//
// The emulator offers no way to script input, and reverse engineering a game's
// dialogue means getting the game to an actual dialogue box. A single
// auto-pressed button cannot do that: Pokemon X's opening asks for choices and
// a name, and a name needs the touch screen.
//
// So a script drives it. The script runs as a Lua coroutine, resumed once per
// HID frame; anything that takes time yields back to the emulator rather than
// blocking it.
//
// Enabled only by the XYORAS_LUA env var naming a script file. Absent, none of
// this does anything.

#pragma once

#include <string>
#include "common/common_types.h"

namespace XyorasScript {

/// Buttons a script can drive. Values match nothing in particular; hid.cpp maps
/// them onto PadState.
enum Button {
    BTN_A = 0,
    BTN_B,
    BTN_X,
    BTN_Y,
    BTN_L,
    BTN_R,
    BTN_ZL,   ///< accepted by name, but see hid.cpp: PadState has no field
    BTN_ZR,   ///< for these, so they are recognised and then ignored
    BTN_START,
    BTN_SELECT,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_COUNT,
};

/// True when a script is loaded and has not finished.
bool IsActive();

/// Advances the script by one frame. Call once per HID update, before reading
/// the state below. Does nothing if no script is loaded.
void Step();

/// Which buttons the script is currently holding, as a bitmask of (1 << Button).
u32 HeldButtons();

/// Where the script is currently touching the bottom screen.
/// Coordinates are in pixels, 0-319 by 0-239. Returns false when not touching.
bool TouchState(u16& x, u16& y);

} // namespace XyorasScript
