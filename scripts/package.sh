#!/usr/bin/env bash
# Packages the plugin into an SD-card-ready archive.
#
#   scripts/package.sh
#
# Output: dist/luma.zip
#
#   luma/plugins/<TitleID>/XYORASAccess.3gx   one copy per supported game
#   xyoras-access/espeak-ng-data/             voice data
#   xyoras-access/sounds/                     non-speech cues
#   xyoras-access/config.txt                  default settings
#
# The same .3gx goes in all four folders; the plugin works out which game it
# is running in at startup.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Re-exec inside devkitPro MSYS2 if needed; see msys-guard.sh for why.
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

PLUGIN="${XYORAS_ROOT}/plugin/XYORASAccess.3gx"
STAGE="${XYORAS_DIST}/sdcard"
ARCHIVE="${XYORAS_DIST}/luma.zip"

# Pokemon X, Y, Omega Ruby, Alpha Sapphire. See "AI docks/03-target-games.md".
TITLE_IDS=(
    0004000000055D00
    0004000000055E00
    000400000011C400
    000400000011C500
)

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -f "${PLUGIN}" ] || die "${PLUGIN} not found — run scripts/build-plugin.sh first"

# -----------------------------------------------------------------------------
# Stage
# -----------------------------------------------------------------------------
#
# The voice data staged by build-espeak-3ds.sh already lives under
# dist/sdcard/xyoras-access/, so clear only the plugin folders rather than the
# whole staging tree.

log "staging"
rm -rf "${STAGE}/luma"

for tid in "${TITLE_IDS[@]}"; do
    mkdir -p "${STAGE}/luma/plugins/${tid}"
    cp "${PLUGIN}" "${STAGE}/luma/plugins/${tid}/"
done

mkdir -p "${STAGE}/xyoras-access/sounds"

if [ ! -d "${STAGE}/xyoras-access/espeak-ng-data" ]; then
    printf '\033[1;33m!!!\033[0m no voice data staged — run scripts/build-espeak-3ds.sh, or the mod will be mute\n' >&2
fi

# Non-speech cues. Absent until Phase 6; the plugin runs without them.
if [ -d "${XYORAS_ROOT}/plugin/data/sounds" ]; then
    cp -r "${XYORAS_ROOT}/plugin/data/sounds/." "${STAGE}/xyoras-access/sounds/"
fi

# -----------------------------------------------------------------------------
# Default config
# -----------------------------------------------------------------------------

if [ ! -f "${STAGE}/xyoras-access/config.txt" ]; then
    log "writing default config"
    cat > "${STAGE}/xyoras-access/config.txt" <<'CONFIG'
# XYORAS Access settings.
# Editable here or from the in-game menu. Lines starting with # are ignored.

# Speech rate in words per minute (80-450).
rate = 200

# Voice pitch (0-100).
pitch = 50

# Output volume (0.0-1.0).
volume = 1.0

# Verbosity: terse | normal | verbose
verbosity = normal

# Short tick sound on each step, and a distinct sound when a step is blocked.
movement_cues = on

# Announce the map name when crossing into a new area.
announce_map_changes = on

# Modifier for the hotkey layer: zl | lr
# ZL exists only on New 3DS; L+R works everywhere.
modifier = zl

# Write a debug log to /xyoras-access/log.txt. Slow — leave off unless needed.
debug_log = off
CONFIG
fi

# -----------------------------------------------------------------------------
# Archive
# -----------------------------------------------------------------------------

log "creating $(basename "${ARCHIVE}")"
rm -f "${ARCHIVE}"

if command -v 7z >/dev/null 2>&1; then
    ( cd "${STAGE}" && 7z a -tzip -bso0 -bsp0 "${ARCHIVE}" luma xyoras-access >/dev/null )
elif command -v zip >/dev/null 2>&1; then
    ( cd "${STAGE}" && zip -qr "${ARCHIVE}" luma xyoras-access )
elif command -v powershell >/dev/null 2>&1; then
    # devkitPro's MSYS2 ships neither 7z nor zip, so this is the branch that
    # normally runs on Windows. PowerShell is a native program and cannot read
    # MSYS paths like /c/Users/..., so translate them first.
    if command -v cygpath >/dev/null 2>&1; then
        _win_stage="$(cygpath -w "${STAGE}")"
        _win_archive="$(cygpath -w "${ARCHIVE}")"
    else
        _win_stage="${STAGE}"
        _win_archive="${ARCHIVE}"
    fi
    powershell -NoProfile -NonInteractive -Command         "Compress-Archive -Path '${_win_stage}\luma','${_win_stage}\xyoras-access' -DestinationPath '${_win_archive}' -Force"
else
    die "no zip tool found (tried 7z, zip, powershell)"
fi

[ -f "${ARCHIVE}" ] || die "archive was not created"

log "packaged ${ARCHIVE} ($(du -h "${ARCHIVE}" | cut -f1))"
echo
echo "  Extract to the root of the SD card, merging with the existing luma folder."
echo "  Then enable the plugin loader in Rosalina (L + Down + Select)."
