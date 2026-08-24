#!/usr/bin/env bash
# Builds the accessible fork of Universal-Updater (open-source homebrew store).
#
#   scripts/build-univupdater.sh [--clean]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

UU_DIR="${UU_DIR:-${HOME}/Documents/git/Universal-Updater}"

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -d "${UU_DIR}" ] || die "checkout not found: ${UU_DIR}"

if [ "${1:-}" = "--clean" ]; then
    log "cleaning"; make -C "${UU_DIR}" clean; exit 0
fi

log "universal-updater"
make -C "${UU_DIR}"

echo
log "built:"
ls -la "${UU_DIR}"/*.3dsx 2>/dev/null || true
