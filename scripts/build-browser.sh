#!/usr/bin/env bash
# Builds browser/app/browser.3dsx -- the accessible SD-card browser.
#
#   scripts/build-browser.sh [--clean]
#
# Standalone homebrew with eSpeak NG linked in and audio via NDSP. No
# CTRPluginFramework and no plugin injection; see
# "AI docks/15-home-menu-screen-reader.md" for why that route is not available.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

APP_DIR="${XYORAS_ROOT}/browser/app"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -f "${XYORAS_ROOT}/dist/espeak-ng-3ds/lib/libespeak-ng.a" ] \
    || die "libespeak-ng.a missing -- run scripts/build-espeak-3ds.sh"

if [ "${1:-}" = "--clean" ]; then
    log "cleaning"
    dkp_make -C "${APP_DIR}" clean
    exit 0
fi

log "browser"
dkp_make -C "${APP_DIR}"

echo
log "built:"
ls -la "${APP_DIR}"/browser.3dsx 2>/dev/null || true
