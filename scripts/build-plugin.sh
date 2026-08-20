#!/usr/bin/env bash
# Builds plugin/XYORASAccess.3gx.
#
#   scripts/build-plugin.sh [--clean] [--check]
#
# Requires scripts/bootstrap.sh and scripts/build-espeak-3ds.sh to have run.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Re-exec inside devkitPro MSYS2 if needed; see msys-guard.sh for why.
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

PLUGIN_DIR="${XYORAS_ROOT}/plugin"
OUTPUT="${PLUGIN_DIR}/XYORASAccess.3gx"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

case "${1:-}" in
    --check)
        dkp_make -C "${PLUGIN_DIR}" check
        exit 0
        ;;
    --clean)
        log "cleaning"
        dkp_make -C "${PLUGIN_DIR}" clean
        ;;
esac

# -----------------------------------------------------------------------------
# Preflight — a missing dependency produces a wall of link errors otherwise
# -----------------------------------------------------------------------------

missing=0
require() {
    if [ ! -e "$2" ]; then
        warn "missing $1: $2"
        missing=1
    fi
}

require "libctrpf"     "${DEVKITPRO}/libctrpf/lib/libctrpf.a"
require "libcwav"      "${DEVKITPRO}/libcwav/lib/libcwav.a"
require "libncsnd"     "${DEVKITPRO}/libncsnd/lib/libncsnd.a"
require "libctru"      "${CTRULIB}/lib/libctru.a"
require "libespeak-ng" "${ESPEAK_NG_DIR}/build-3ds/src/libespeak-ng/libespeak-ng.a"

if ! command -v 3gxtool >/dev/null 2>&1 && [ ! -x "${DEVKITPRO}/tools/bin/3gxtool.exe" ]; then
    warn "missing 3gxtool"
    missing=1
fi

if [ "${missing}" -ne 0 ]; then
    die "dependencies missing — run scripts/bootstrap.sh and scripts/build-espeak-3ds.sh"
fi

# -----------------------------------------------------------------------------
# Build
# -----------------------------------------------------------------------------

log "building XYORASAccess.3gx"
dkp_make -C "${PLUGIN_DIR}" -j"$(nproc 2>/dev/null || echo 4)"

[ -f "${OUTPUT}" ] || die "make finished but ${OUTPUT} was not produced"

log "built $(basename "${OUTPUT}") ($(du -h "${OUTPUT}" | cut -f1))"

# Keep the .elf and .map: a crash address on hardware is meaningless without
# them. See "AI docks/10-testing-and-qa.md".
if [ -f "${PLUGIN_DIR}/XYORASAccess.map" ]; then
    log "symbols retained at plugin/XYORASAccess.map — keep this with the build you test"
fi

log "done — next: scripts/package.sh"
