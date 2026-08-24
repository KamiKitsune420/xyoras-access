#!/usr/bin/env bash
# Installs ScreenReader.3gx into Azahar's SD card and launches the test host.
#
#   scripts/run-screenreader.sh              install, then launch Azahar
#   scripts/run-screenreader.sh --install    install only, do not launch
#
# Paths come from scripts/env.local.sh, same as run-emulator.sh:
#   XYORAS_AZAHAR       path to azahar.exe
#   XYORAS_AZAHAR_USER  Azahar user directory
#
# Two things must be true of that azahar.exe, or the plugin silently will not
# load (see "AI docks/15-home-menu-screen-reader.md"):
#
#   1. It must be built with the AZAHAR_PLUGIN_ANY_TITLE patch to plgldr.cpp.
#      Stock Azahar refuses to load plugins into homebrew at all.
#   2. The plugin loader must be enabled in its System settings.
#
# Audio: you will NOT hear speech. Azahar does not render CSND. With the CSND
# tap patch it writes csnd_dump_N.wav into the user directory instead; play
# those with scripts/play-speech.sh.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m !\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

# shellcheck source=/dev/null
[ -f "${SCRIPT_DIR}/env.local.sh" ] && source "${SCRIPT_DIR}/env.local.sh"

AZAHAR="${XYORAS_AZAHAR:-}"
USERDIR="${XYORAS_AZAHAR_USER:-}"
[ -n "${USERDIR}" ] || die "XYORAS_AZAHAR_USER not set (scripts/env.local.sh)"

PLUGIN="${ROOT}/plugin-screenreader/ScreenReader.3gx"
CFG="${ROOT}/plugin-screenreader/screenreader.cfg"
TESTAPP="${ROOT}/plugin-screenreader/testapp/testapp.cxi"

# Title id from testapp.rsf (UniqueId 0xF0001). Its high half is 0x00040000,
# so it satisfies plgldr's title mask on its own.
TITLE_ID="000400000F000100"

for f in "${PLUGIN}" "${CFG}" "${TESTAPP}"; do
    [ -f "${f}" ] || die "$(basename "${f}") missing — run scripts/build-screenreader.sh first"
done

# Stale-build guard: an edited source with an old .3gx is the single most
# confusing failure, because everything looks installed and nothing changed.
NEWER="$(find "${ROOT}/plugin-screenreader/source" "${ROOT}/plugin-screenreader/include" \
         -newer "${PLUGIN}" -name '*.[ch]pp' -print -quit 2>/dev/null)"
[ -n "${NEWER}" ] && warn "${NEWER} is newer than ScreenReader.3gx — rebuild first"

# Installed under the test host's own title id, NEVER as default.3gx.
# default.3gx loads into EVERY title, so a crashing build takes down whatever
# you launch next -- including the Pokemon games.
mkdir -p "${USERDIR}/sdmc/luma/plugins/${TITLE_ID}"
cp "${PLUGIN}" "${USERDIR}/sdmc/luma/plugins/${TITLE_ID}/ScreenReader.3gx"
rm -f "${USERDIR}/sdmc/luma/plugins/default.3gx"
log "installed to luma/plugins/${TITLE_ID}/"

cp "${CFG}" "${USERDIR}/sdmc/screenreader.cfg"
log "installed screenreader.cfg ($(grep -o '0x[0-9a-fA-F]*' "${CFG}" | head -1))"

# libctru refuses to initialise ndsp unless this file exists, and fails with a
# bare DSP result code that says nothing about the cause. Azahar HLEs the DSP so
# the contents are never read -- an empty file is enough. Without it, standalone
# homebrew audio cannot work at all. (Game plugins never hit this: they go
# through CSND, not libctru's ndsp.)
if [ ! -f "${USERDIR}/sdmc/3ds/dspfirm.cdc" ]; then
    mkdir -p "${USERDIR}/sdmc/3ds"
    : > "${USERDIR}/sdmc/3ds/dspfirm.cdc"
    log "created empty sdmc:/3ds/dspfirm.cdc (required for ndsp)"
fi

if [ ! -d "${USERDIR}/sdmc/xyoras-access/espeak-ng-data" ]; then
    warn "espeak-ng-data missing from the SD card — speech will fail to init"
    warn "run scripts/package.sh, or copy dist/espeak-ng-3ds/espeak-ng-data there"
fi

if [ "${1:-}" = "--install" ]; then
    log "install only; not launching"
    exit 0
fi

[ -n "${AZAHAR}" ] || die "XYORAS_AZAHAR not set (scripts/env.local.sh)"
[ -f "${AZAHAR}" ] || die "azahar.exe not found: ${AZAHAR}"

log "launching — move the cursor with up/down, START to exit"
AZAHAR_PLUGIN_ANY_TITLE=1 "${AZAHAR}" "${TESTAPP}" &
wait
