#!/usr/bin/env bash
# Builds the accessible fork of FBI (3DS title/file manager).
#
#   scripts/build-fbi.sh [--clean]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

FBI_DIR="${FBI_DIR:-${HOME}/Documents/git/FBI}"

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -d "${FBI_DIR}" ] || die "checkout not found: ${FBI_DIR}"

if [ "${1:-}" = "--clean" ]; then
    log "cleaning"; make -C "${FBI_DIR}" clean OS=Windows_NT; exit 0
fi

log "fbi"
# buildtools/make_base detects the host with `ifeq ($(OS),Windows_NT)`, and that
# variable does not survive the msys-guard re-exec into devkitPro's shell -- so
# it falls through to `uname -s`, gets MINGW64_NT, and errors "Unsupported host
# OS". Passing it on the command line is enough.
make -C "${FBI_DIR}" OS=Windows_NT

echo
log "built:"
ls -la "${FBI_DIR}"/output/*.3dsx 2>/dev/null || ls -la "${FBI_DIR}"/*.3dsx 2>/dev/null || true
