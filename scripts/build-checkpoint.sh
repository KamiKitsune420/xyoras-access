#!/usr/bin/env bash
# Builds the accessible fork of Checkpoint (3DS save manager).
#
#   scripts/build-checkpoint.sh [--clean]
#
# Override the checkout location with CHECKPOINT_DIR.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

CHECKPOINT_DIR="${CHECKPOINT_DIR:-${HOME}/Documents/git/Checkpoint}"

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -d "${CHECKPOINT_DIR}/3ds" ] || die "Checkpoint checkout not found: ${CHECKPOINT_DIR}"

if [ "${1:-}" = "--clean" ]; then
    log "cleaning"
    make -C "${CHECKPOINT_DIR}/3ds" clean
    exit 0
fi

# Build only the 3DS target; the repo's root Makefile also builds the Switch
# version, which needs devkitA64 and is not what we are patching.
log "checkpoint (3ds)"
make -C "${CHECKPOINT_DIR}/3ds" \
    VERSION_MAJOR=5 VERSION_MINOR=1 VERSION_MICRO=0 GIT_REV=a11y

echo
log "built:"
ls -la "${CHECKPOINT_DIR}/3ds"/*.3dsx 2>/dev/null || true
