-- Drive Pokemon X forward and report what the mod can see at each step.
--
-- Runs inside the patched Azahar (see tools/azahar-lua-patch/). The emulator
-- resumes this once per pad update; anything that takes time yields, so the
-- game runs normally in between.
--
-- The trick here is that the mod is the observer. Tapping the modifier alone
-- is its read-screen command, which makes it write everything it can see into
-- /xyoras-access/narration.txt. So the script does not need to know what is on
-- screen -- it presses on, asks the mod what it sees, and leaves a trail that
-- says how far it got and where it stopped.
--
-- The modifier is L+R rather than ZL: ZL is a New 3DS button carried by the IR
-- service, and the emulator's PadState has no field for it.

local READ_SCREEN = {"l", "r"}

log("waiting for the game to boot")
seconds(10)

log("asking what is on screen before touching anything")
chord(READ_SCREEN, 12)
seconds(4)

-- Press A repeatedly. That is enough for the language screen, its confirm, the
-- title, and any dialogue that only wants acknowledging. It is NOT enough for
-- character selection or name entry, and finding out exactly where it stops is
-- the point of this run.
for i = 1, 40 do
    press("a", 8)
    seconds(2)

    if i % 4 == 0 then
        log("after " .. i .. " presses of A")
        chord(READ_SCREEN, 12)
        seconds(4)
    end
end

log("A alone got us as far as it goes; taking a layout snapshot")
chord({"l", "r", "x"}, 12)
seconds(6)

log("done")
