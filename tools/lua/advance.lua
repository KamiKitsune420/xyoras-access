-- Drive Pokemon X forward and report what the mod can see at each step.
--
-- Runs inside the patched Azahar (see tools/azahar-lua-patch/). The emulator
-- resumes this once per pad update; anything that takes time yields, so the
-- game runs normally in between.
--
-- The trick here is that the mod is the observer. Tapping the modifier alone is
-- its read-screen command, which makes it write everything it can see into
-- narration.txt. So the script does not need to know what is on screen -- it
-- presses on, asks the mod what it sees, and reads the answer back.
--
-- The modifier is L+R rather than ZL: ZL is a New 3DS button carried by the IR
-- service, and the emulator's PadState has no field for it.

local SD = os.getenv("XYORAS_SD_HOST")
local TRACE = SD and (SD .. "/narration.txt") or nil

local READ_SCREEN = {"l", "r"}

--- Everything the mod reported the last time it was asked to read the screen.
-- The trace is append-only, so the last block is the most recent answer.
local function last_read()
    if not TRACE then
        return nil
    end
    local body = readfile(TRACE)
    if not body then
        return nil
    end

    -- Collect the lines of the final "read screen:" block. Its entries are the
    -- ones prefixed "  | "; anything else ends the block.
    local lines = {}
    for line in body:gmatch("[^\r\n]+") do
        if line:match("^read screen:") then
            lines = {line}
        elseif #lines > 0 and line:match("^  | ") then
            lines[#lines + 1] = line:gsub("^  | ", "")
        elseif #lines > 0 and not line:match("^  ") then
            -- block finished; keep it and carry on looking for a later one
        end
    end
    return lines
end

--- Ask the mod what is on screen, then say so in the script's own log.
local function observe(label)
    chord(READ_SCREEN, 12)
    seconds(3)   -- give the poll loop time to notice and write

    local seen = last_read()
    if not seen or #seen == 0 then
        log(label .. ": mod reported nothing")
        return
    end
    log(label .. ": " .. seen[1])
    for i = 2, math.min(#seen, 8) do
        log("    " .. seen[i])
    end
    if #seen > 9 then
        log("    ... and " .. (#seen - 9) .. " more")
    end
end

log("waiting for the game to boot")
seconds(12)

observe("before touching anything")

-- Press A repeatedly. That is enough for the language screen, its confirm, the
-- title, and any dialogue that only wants acknowledging. It is NOT enough for
-- character selection or name entry, and finding out exactly where it stops is
-- the whole point of this run.
for i = 1, 40 do
    press("a", 8)
    seconds(2)

    if i % 4 == 0 then
        observe("after " .. i .. " presses of A")
    end
end

-- If A alone stalled, the screen wants something else. Try the other things a
-- menu might be waiting for, one at a time, checking after each.
log("A alone is spent; trying other inputs")

press("start", 8)
seconds(2)
observe("after Start")

for _ = 1, 3 do
    press("down", 8)
    seconds(1)
end
press("a", 8)
seconds(2)
observe("after Down x3 then A")

-- A tap in the middle of the bottom screen. Plenty of screens are driven
-- entirely by touch and ignore every button.
tap(160, 120)
seconds(2)
observe("after tapping the middle of the bottom screen")

log("taking a layout snapshot before finishing")
chord({"l", "r", "x"}, 12)
seconds(6)

log("done")
