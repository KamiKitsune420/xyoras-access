#!/usr/bin/env bash
# Packages the plugin into an SD-card-ready archive.
#
#   scripts/package.sh                 a normal release package
#   scripts/package.sh --first-boot    the same, plus diagnostic markers
#
# Output: dist/luma.zip
#
#   luma/plugins/<TitleID>/XYORASAccess.3gx   one copy per supported game
#   xyoras-access/espeak-ng-data/             voice data
#   xyoras-access/sounds/                     non-speech cues
#
# --first-boot additionally writes the `self-test` and `trace-narration`
# marker files. They are what the bring-up sequence in
# "AI docks/10-testing-and-qa.md" asks for, and creating an extension-less
# empty file by hand on Windows is more annoying than it should be. They are
# NOT in a normal package: they cost a player SD writes and buy them nothing.
#
# The same .3gx goes in all four folders; the plugin works out which game it
# is running in at startup.

set -euo pipefail

FIRST_BOOT=0
for arg in "$@"; do
    case "${arg}" in
        --first-boot) FIRST_BOOT=1 ;;
        *) printf 'unknown option: %s\n' "${arg}" >&2; exit 2 ;;
    esac
done

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

# Diagnostic markers. Removed unless asked for, so a package built after a
# --first-boot one does not quietly keep them.
rm -f "${STAGE}/xyoras-access/self-test" \
      "${STAGE}/xyoras-access/trace-narration" \
      "${STAGE}/xyoras-access/dump-audio"

if [ "${FIRST_BOOT}" -eq 1 ]; then
    # self-test       writes diagnostics.txt: what was detected, whether speech
    #                 started, how long synthesis took.
    # trace-narration writes narration.txt: every scan, what it read, what it
    #                 chose to say.
    #
    # dump-audio is deliberately NOT written. It diverts speech to .wav files,
    # and whether CSND plays over a running game is the one thing hardware has
    # to answer. Add it by hand only if the first boot is silent.
    : > "${STAGE}/xyoras-access/self-test"
    : > "${STAGE}/xyoras-access/trace-narration"
    log "including first-boot diagnostic markers"
fi

if [ ! -d "${STAGE}/xyoras-access/espeak-ng-data" ]; then
    printf '\033[1;33m!!!\033[0m no voice data staged — run scripts/build-espeak-3ds.sh, or the mod will be mute\n' >&2
fi

# Non-speech cues. Absent until Phase 6; the plugin runs without them.
if [ -d "${XYORAS_ROOT}/plugin/data/sounds" ]; then
    cp -r "${XYORAS_ROOT}/plugin/data/sounds/." "${STAGE}/xyoras-access/sounds/"
fi

# -----------------------------------------------------------------------------
# Settings
# -----------------------------------------------------------------------------
#
# There is deliberately no config.txt. One used to be written here, documenting
# rate, pitch, volume, verbosity and the modifier key -- and nothing in the
# plugin ever read it. Editing it did nothing, silently, which is worse than
# having no settings at all: a player could reasonably conclude the mod was
# broken rather than that the file was.
#
# It comes back when there is code to load it. Until then the compiled defaults
# (200 wpm, pitch 50, voice "en") are the settings.

# Remove one left over from an earlier package, so upgrading does not leave a
# file that quietly does nothing.
rm -f "${STAGE}/xyoras-access/config.txt"

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

if [ "${FIRST_BOOT}" -eq 1 ]; then
    echo
    echo "  First-boot markers included. After booting, copy these back off the card:"
    echo "    xyoras-access/checkpoints.txt   how far startup got"
    echo "    xyoras-access/diagnostics.txt   what was detected, and speech timings"
    echo "    xyoras-access/narration.txt     every scan, and what it read"
    echo
    echo "  The sequence to work through is in AI docks/10-testing-and-qa.md."
fi
