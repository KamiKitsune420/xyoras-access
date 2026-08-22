// XYORAS Access — Lua input scripting for the emulator.
// Licensed under GPLv2 or any later version, as the rest of Azahar.
//
// See xyoras_script.h for why this exists.
//
// Shape of it: the script is loaded into a Lua coroutine. Once per HID frame
// Step() resumes it, and the script yields whenever it wants time to pass. So
// `wait(30)` returns control to the emulator for thirty frames rather than
// spinning, and the script reads as a straight sequence of actions.
//
// The C side is deliberately tiny -- hold, release, wait, touch, log, peek.
// Everything convenient (press, tap, holding a chord for a while) is defined in
// Lua in the prelude below, because it is far easier to get right there.

#include "core/xyoras_script.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include "common/logging/log.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace XyorasScript {

namespace {

struct State {
    lua_State* L = nullptr;
    lua_State* co = nullptr;      ///< the coroutine the script runs in
    bool loaded = false;
    bool finished = false;

    u32 held = 0;                 ///< buttons currently held
    bool touching = false;
    u16 touch_x = 0;
    u16 touch_y = 0;

    u32 wait_frames = 0;          ///< frames left before the script is resumed
    u64 frame = 0;

    std::FILE* log = nullptr;     ///< a copy of the script's own log()
};

State g_state;

const char* const kButtonNames[BTN_COUNT] = {
    "a", "b", "x", "y", "l", "r", "zl", "zr",
    "start", "select", "up", "down", "left", "right",
};

int ButtonFromName(const char* name) {
    for (int i = 0; i < BTN_COUNT; ++i) {
        if (std::strcmp(name, kButtonNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void ScriptLog(const std::string& line) {
    LOG_INFO(Service_HID, "XYORAS LUA: {}", line);
    if (g_state.log) {
        std::fprintf(g_state.log, "%s\n", line.c_str());
        std::fflush(g_state.log);   // flushed per line: a crash must not lose it
    }
}

// -- the functions Lua can call ----------------------------------------------

int L_hold(lua_State* L) {
    const int b = ButtonFromName(luaL_checkstring(L, 1));
    if (b < 0) {
        return luaL_error(L, "unknown button '%s'", lua_tostring(L, 1));
    }
    g_state.held |= (1u << b);
    return 0;
}

int L_release(lua_State* L) {
    const int b = ButtonFromName(luaL_checkstring(L, 1));
    if (b < 0) {
        return luaL_error(L, "unknown button '%s'", lua_tostring(L, 1));
    }
    g_state.held &= ~(1u << b);
    return 0;
}

int L_release_all(lua_State* L) {
    g_state.held = 0;
    g_state.touching = false;
    return 0;
}

/// wait(frames) -- yields; the emulator runs `frames` frames before resuming.
int L_wait(lua_State* L) {
    lua_Integer n = luaL_optinteger(L, 1, 1);
    if (n < 1) {
        n = 1;
    }
    g_state.wait_frames = static_cast<u32>(n);
    return lua_yield(L, 0);
}

/// touch(x, y) -- bottom screen, 0-319 by 0-239. touch() with no arguments
/// lifts.
int L_touch(lua_State* L) {
    if (lua_gettop(L) == 0) {
        g_state.touching = false;
        return 0;
    }
    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 319) x = 319;
    if (y > 239) y = 239;
    g_state.touch_x = static_cast<u16>(x);
    g_state.touch_y = static_cast<u16>(y);
    g_state.touching = true;
    return 0;
}

int L_log(lua_State* L) {
    ScriptLog(luaL_checkstring(L, 1));
    return 0;
}

int L_frame(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(g_state.frame));
    return 1;
}

/// Reads a file the guest wrote, so a script can react to what the plugin saw
/// rather than guessing at timings. Returns nil if it cannot be read.
int L_readfile(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

/// Convenience built on the primitives above. Written in Lua because getting
/// press-and-release timing right in C is more code and no clearer.
const char* const kPrelude = R"LUA(
-- Press a button for `frames` (default 8) and let go, with a short gap after.
-- Menus need to see an edge, so a button that is simply held does nothing.
function press(button, frames)
    hold(button)
    wait(frames or 8)
    release(button)
    wait(4)
end

-- Hold several buttons at once, then let go. For the mod's own chords:
--   chord({"l", "r"}, 10)         -- the modifier, tapped: read the screen
--   chord({"l", "r", "x"}, 10)    -- modifier + X: layout snapshot
function chord(buttons, frames)
    for _, b in ipairs(buttons) do hold(b) end
    wait(frames or 10)
    for _, b in ipairs(buttons) do release(b) end
    wait(6)
end

-- Tap the bottom screen at a point.
function tap(x, y, frames)
    touch(x, y)
    wait(frames or 8)
    touch()
    wait(4)
end

-- Seconds, at the 3DS pad-update rate.
function seconds(s)
    wait(math.floor(s * 60))
end
)LUA";

bool Load() {
    const char* path = std::getenv("XYORAS_LUA");
    if (!path || !*path) {
        return false;
    }

    g_state.L = luaL_newstate();
    if (!g_state.L) {
        LOG_ERROR(Service_HID, "XYORAS LUA: could not create a Lua state");
        return false;
    }
    luaL_openlibs(g_state.L);

    lua_register(g_state.L, "hold", L_hold);
    lua_register(g_state.L, "release", L_release);
    lua_register(g_state.L, "release_all", L_release_all);
    lua_register(g_state.L, "wait", L_wait);
    lua_register(g_state.L, "touch", L_touch);
    lua_register(g_state.L, "log", L_log);
    lua_register(g_state.L, "frame", L_frame);
    lua_register(g_state.L, "readfile", L_readfile);

    if (const char* log_path = std::getenv("XYORAS_LUA_LOG")) {
        g_state.log = std::fopen(log_path, "w");
    }

    if (luaL_dostring(g_state.L, kPrelude) != LUA_OK) {
        LOG_ERROR(Service_HID, "XYORAS LUA: prelude failed: {}",
                  lua_tostring(g_state.L, -1));
        return false;
    }

    // The script itself goes on a coroutine so that wait() can yield.
    g_state.co = lua_newthread(g_state.L);
    if (luaL_loadfile(g_state.co, path) != LUA_OK) {
        LOG_ERROR(Service_HID, "XYORAS LUA: could not load {}: {}", path,
                  lua_tostring(g_state.co, -1));
        return false;
    }

    LOG_INFO(Service_HID, "XYORAS LUA: running {}", path);
    ScriptLog(std::string("--- running ") + path + " ---");
    return true;
}

} // namespace

bool IsActive() {
    return g_state.loaded && !g_state.finished;
}

void Step() {
    if (!g_state.loaded) {
        static bool tried = false;
        if (tried) {
            return;
        }
        tried = true;
        g_state.loaded = Load();
        if (!g_state.loaded) {
            g_state.finished = true;
            return;
        }
    }

    if (g_state.finished) {
        return;
    }

    ++g_state.frame;

    if (g_state.wait_frames > 0) {
        --g_state.wait_frames;
        return;
    }

    int nresults = 0;
    const int status = lua_resume(g_state.co, g_state.L, 0, &nresults);

    if (status == LUA_YIELD) {
        return;     // wait() asked for time; state stays as the script left it
    }

    if (status != LUA_OK) {
        ScriptLog(std::string("ERROR: ") + lua_tostring(g_state.co, -1));
        LOG_ERROR(Service_HID, "XYORAS LUA: {}", lua_tostring(g_state.co, -1));
    } else {
        ScriptLog("--- script finished ---");
    }

    // Either way the script is done. Let go of everything, or the game is left
    // with a button stuck down.
    g_state.finished = true;
    g_state.held = 0;
    g_state.touching = false;
}

u32 HeldButtons() {
    return IsActive() ? g_state.held : 0;
}

bool TouchState(u16& x, u16& y) {
    if (!IsActive() || !g_state.touching) {
        return false;
    }
    x = g_state.touch_x;
    y = g_state.touch_y;
    return true;
}

} // namespace XyorasScript
