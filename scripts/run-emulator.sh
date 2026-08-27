#!/usr/bin/env bash
# Runs the plugin in Azahar and collects what it did.
#
#   scripts/run-emulator.sh [seconds]   run unattended, then report (default 120)
#   scripts/run-emulator.sh manual      launch and leave it up for you to play
#   scripts/run-emulator.sh report      read the trace from a manual session
#   scripts/run-emulator.sh lua <file> [seconds]
#                                       drive the game with a Lua script
#
# "lua" is what gets past an intro. A script can press buttons AND touch the
# bottom screen, which is the only way through a name-entry keyboard, and it
# can read the mod's own trace to find out what is on screen rather than
# guessing at timings. See tools/azahar-lua-patch/.
#
# "manual" is the one that matters for anything involving menus or dialogue.
# Auto-pressing a button cannot navigate a game, so an unattended run only ever
# sees whatever screen it happens to land on. A person playing produces real
# screen changes, which is the only way to see whether the mod notices them.
#
# The emulator cannot play the mod's audio -- no 3DS emulator implements CSND,
# and CSND is the only audio path a game plugin has. See
# "AI docks/06-tts-audio-pipeline.md". So this run diverts speech to .wav files
# instead, which scripts/play-speech.sh will play through the PC.
#
# What this DOES test: everything except the final hardware hop. Whether the
# plugin loads, whether SD access works inside a game process, whether eSpeak
# starts and synthesises, whether the heap scan finds text panes, what text it
# reads, and what it decides to say.
#
# Paths are personal, so set them once in scripts/env.local.sh (gitignored):
#
#   XYORAS_AZAHAR=/c/path/to/azahar.exe
#   XYORAS_ROM=/c/path/to/Pokemon X.3ds
#   XYORAS_AZAHAR_USER=/c/Users/<you>/AppData/Roaming/Azahar
#
# Anything already in the environment wins over that file.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m !\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

# shellcheck source=/dev/null
[ -f "${SCRIPT_DIR}/env.local.sh" ] && source "${SCRIPT_DIR}/env.local.sh"

MODE="${1:-120}"
LUA_SCRIPT=""

if [ "${MODE}" = "lua" ]; then
    LUA_SCRIPT="${2:-}"
    [ -n "${LUA_SCRIPT}" ] || { printf 'usage: %s lua <script.lua> [seconds]
' "$0" >&2; exit 2; }
    [ -f "${LUA_SCRIPT}" ] || { printf 'no such script: %s
' "${LUA_SCRIPT}" >&2; exit 2; }
    MODE="${3:-180}"
fi

AZAHAR="${XYORAS_AZAHAR:-}"
ROM="${XYORAS_ROM:-}"
USERDIR="${XYORAS_AZAHAR_USER:-${APPDATA:-${HOME}/AppData/Roaming}/Azahar}"

[ -n "${AZAHAR}" ] || die "XYORAS_AZAHAR is not set — see the comments at the top of this script"
[ -x "${AZAHAR}" ] || die "not executable: ${AZAHAR}"
[ -n "${ROM}" ]    || die "XYORAS_ROM is not set — see the comments at the top of this script"
[ -f "${ROM}" ]    || die "no such ROM: ${ROM}"

PLUGIN="${ROOT}/plugin/XYORASAccess.3gx"
[ -f "${PLUGIN}" ] || die "${PLUGIN} not found — run scripts/build-plugin.sh first"

# Refuse to deploy a binary older than the sources.
#
# A failed build leaves the previous .3gx in place, and this script happily
# deployed and launched it -- so a compile error looked exactly like "the change
# did not work", which cost a whole round trip. Better to stop.
if [ "${MODE:-}" != "report" ]; then
    NEWER="$(find "${ROOT}/plugin/source" "${ROOT}/plugin/include"                   -newer "${PLUGIN}" -name '*.[ch]pp' -print -quit 2>/dev/null)"
    if [ -n "${NEWER}" ]; then
        die "${PLUGIN} is older than $(basename "${NEWER}") — the last build failed.
Run scripts/build-plugin.sh and read the errors."
    fi
fi

# Pokemon X. The emulator runs one title at a time, so only this folder matters.
TITLE_ID="0004000000055D00"
SD="${USERDIR}/sdmc/xyoras-access"
PLUGIN_DIR="${USERDIR}/sdmc/luma/plugins/${TITLE_ID}"

report() {
    echo
    log "checkpoints — how far startup got"
    if [ -f "${SD}/checkpoints.txt" ]; then
        sed 's/^/    /' "${SD}/checkpoints.txt"
    else
        warn "none written — the plugin did not run at all"
    fi
    
    echo
    log "what it read and said"
    if [ -f "${SD}/narration.txt" ]; then
        grep -v '^  ' "${SD}/narration.txt" | grep -v '^scan: ' | sed 's/^/    /' | head -40
        printf '    (%s lines total, %s scans)\n' \
            "$(wc -l < "${SD}/narration.txt")" \
            "$(grep -c '^scan: ' "${SD}/narration.txt")"
    else
        warn "no narration trace — narration did not start"
    fi
    
    echo
    if [ -f "${SD}/lua.log" ]; then
        log "what the script did"
        sed 's/^/    /' "${SD}/lua.log"
        echo
    fi

    log "speech written"
    if [ -d "${SD}/speech" ] && [ -n "$(ls -A "${SD}/speech" 2>/dev/null)" ]; then
        ls -la "${SD}/speech" | tail -n +4 | sed 's/^/    /'
        echo
        echo "    Hear it:  scripts/play-speech.sh"
    else
        warn "nothing synthesised"
    fi
}

mkdir -p "${SD}" "${PLUGIN_DIR}"

# Reporting reads a session that already happened; it must not deploy over it
# or clear the very trace it is about to read.
if [ "${MODE}" = "report" ]; then
    report
    exit 0
fi

log "deploying $(basename "${PLUGIN}") ($(du -h "${PLUGIN}" | cut -f1))"
cp "${PLUGIN}" "${PLUGIN_DIR}/"

# Clear the previous run so nothing old is mistaken for new.
rm -f "${SD}/checkpoints.txt" "${SD}/diagnostics.txt" "${SD}/narration.txt"
rm -rf "${SD}/speech"

# Markers. dump-audio is what makes the run audible at all: CSND is stubbed in
# every emulator, so without it the speech goes nowhere observable.
: > "${SD}/self-test"
: > "${SD}/trace-narration"
: > "${SD}/dump-audio"

# Press buttons on a slow cycle so the game walks through its own text rather
# than sitting on one screen with nothing changing for the mod to notice.
#
# The names are LOWERCASE and the patched emulator silently ignores anything
# else -- "A" and "DPadDown" match nothing, which cost several runs that looked
# like the mod was failing when the game simply never received any input.
# Accepted: a, b, start, up, down, left, right.
#
# A comma-separated list cycles one button per press, because a single repeated
# button cannot get through a screen that needs a cursor moved and then a
# confirm.
if [ -n "${LUA_SCRIPT}" ]; then
    # A script and the auto-press hack are alternatives; running both means two
    # things fighting over the same buttons.
    unset XYORAS_AUTO_PRESS || true
    export XYORAS_LUA
    XYORAS_LUA="$(cygpath -w "${LUA_SCRIPT}" 2>/dev/null || echo "${LUA_SCRIPT}")"
    export XYORAS_LUA_LOG
    XYORAS_LUA_LOG="$(cygpath -w "${SD}/lua.log" 2>/dev/null || echo "${SD}/lua.log")"
    # The script runs on the host, so it needs the host path to the guest's SD
    # card in order to read back what the mod wrote.
    export XYORAS_SD_HOST
    XYORAS_SD_HOST="$(cygpath -w "${SD}" 2>/dev/null || echo "${SD}")"
    rm -f "${SD}/lua.log"
    log "driving with $(basename "${LUA_SCRIPT}")"
elif [ "${MODE}" != "manual" ]; then
    export XYORAS_AUTO_PRESS="${XYORAS_AUTO_PRESS:-a}"
fi

if [ "${MODE}" = "manual" ]; then
    log "launching — play it, then run: scripts/run-emulator.sh report"
    echo "    Nothing is auto-pressed, so the screens change only when you do it."
    echo "    Mod + X (ZL+X, or L+R+X) writes a layout snapshot to the trace."
    "${AZAHAR}" "${ROM}" &
    disown
    exit 0
fi

if [ -n "${LUA_SCRIPT}" ]; then
    log "running for ${MODE}s (driven by $(basename "${LUA_SCRIPT}"))"
else
    log "running for ${MODE}s (auto-pressing ${XYORAS_AUTO_PRESS})"
fi
"${AZAHAR}" "${ROM}" &
APP_PID=$!

sleep "${MODE}"

kill "${APP_PID}" 2>/dev/null
sleep 2
taskkill //F //IM azahar.exe >/dev/null 2>&1
sleep 1

report
