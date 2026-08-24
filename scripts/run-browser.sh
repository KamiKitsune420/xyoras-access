#!/usr/bin/env bash
# Launches the accessible browser in Azahar.
#
#   scripts/run-browser.sh
#
# Paths come from scripts/env.local.sh (XYORAS_AZAHAR, XYORAS_AZAHAR_USER).

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
APP="${ROOT}/browser/app/browser.3dsx"

[ -n "${USERDIR}" ] || die "XYORAS_AZAHAR_USER not set (scripts/env.local.sh)"
[ -f "${APP}" ]     || die "browser.3dsx missing -- run scripts/build-browser.sh"

# libctru refuses to start ndsp unless this exists, and reports only a bare DSP
# result code that names no cause. Azahar HLEs the DSP, so the contents are
# never read and an empty file is enough.
if [ ! -f "${USERDIR}/sdmc/3ds/dspfirm.cdc" ]; then
    mkdir -p "${USERDIR}/sdmc/3ds"
    : > "${USERDIR}/sdmc/3ds/dspfirm.cdc"
    log "created empty sdmc:/3ds/dspfirm.cdc (required for ndsp)"
fi

if [ ! -d "${USERDIR}/sdmc/xyoras-access/espeak-ng-data" ]; then
    warn "espeak-ng-data missing from the virtual SD -- speech will not start"
    warn "copy dist/espeak-ng-3ds/espeak-ng-data to ${USERDIR}/sdmc/xyoras-access/"
fi

[ -n "${AZAHAR}" ] || die "XYORAS_AZAHAR not set (scripts/env.local.sh)"

log "launching -- up/down move, A open, B back, R repeat, START exit"
"${AZAHAR}" "${APP}" &
wait
