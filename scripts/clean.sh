#!/usr/bin/env bash
# Removes build output.
#
#   scripts/clean.sh          plugin build output only
#   scripts/clean.sh --all    also the eSpeak build and the dist staging tree

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Re-exec inside devkitPro MSYS2 if needed; see msys-guard.sh for why.
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

log "cleaning the plugin"
dkp_make -C "${XYORAS_ROOT}/plugin" clean 2>/dev/null || true
rm -rf "${XYORAS_ROOT}/plugin/build"

if [ "${1:-}" = "--all" ]; then
    log "cleaning the eSpeak NG build"
    rm -rf "${ESPEAK_NG_DIR}/build-3ds"
    log "cleaning dist"
    rm -rf "${XYORAS_DIST}"
fi

log "done"
