#!/usr/bin/env bash
# Runs the plugin in Azahar and collects what it did.
#
#   scripts/run-emulator.sh [seconds]        default 120
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

RUNTIME="${1:-120}"

AZAHAR="${XYORAS_AZAHAR:-}"
ROM="${XYORAS_ROM:-}"
USERDIR="${XYORAS_AZAHAR_USER:-${APPDATA:-${HOME}/AppData/Roaming}/Azahar}"

[ -n "${AZAHAR}" ] || die "XYORAS_AZAHAR is not set — see the comments at the top of this script"
[ -x "${AZAHAR}" ] || die "not executable: ${AZAHAR}"
[ -n "${ROM}" ]    || die "XYORAS_ROM is not set — see the comments at the top of this script"
[ -f "${ROM}" ]    || die "no such ROM: ${ROM}"

PLUGIN="${ROOT}/plugin/XYORASAccess.3gx"
[ -f "${PLUGIN}" ] || die "${PLUGIN} not found — run scripts/build-plugin.sh first"

# Pokemon X. The emulator runs one title at a time, so only this folder matters.
TITLE_ID="0004000000055D00"
SD="${USERDIR}/sdmc/xyoras-access"
PLUGIN_DIR="${USERDIR}/sdmc/luma/plugins/${TITLE_ID}"

mkdir -p "${SD}" "${PLUGIN_DIR}"

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

# Press A on a slow cycle so the game walks through its own text rather than
# sitting on one screen. It is not reliable -- see 12-research-log.md -- but it
# costs nothing.
export XYORAS_AUTO_PRESS="${XYORAS_AUTO_PRESS:-A}"

log "running for ${RUNTIME}s (auto-pressing ${XYORAS_AUTO_PRESS})"
"${AZAHAR}" "${ROM}" &
APP_PID=$!

sleep "${RUNTIME}"

kill "${APP_PID}" 2>/dev/null
sleep 2
taskkill //F //IM azahar.exe >/dev/null 2>&1
sleep 1

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
log "speech written"
if [ -d "${SD}/speech" ] && [ -n "$(ls -A "${SD}/speech" 2>/dev/null)" ]; then
    ls -la "${SD}/speech" | tail -n +4 | sed 's/^/    /'
    echo
    echo "    Hear it:  scripts/play-speech.sh"
else
    warn "nothing synthesised"
fi
