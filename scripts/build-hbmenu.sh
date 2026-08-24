#!/usr/bin/env bash
# Builds the accessible fork of the official Homebrew Launcher.
#
#   scripts/build-hbmenu.sh [--clean]
#
# hbmenu itself is unusable without sight, which means even launching an
# accessible app requires help the first time. This fork links eSpeak NG in and
# announces the entry under the cursor.
#
# The checkout lives outside this repo (it is a fork of devkitPro/3ds-hbmenu);
# override with HBMENU_DIR.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

HBMENU_DIR="${HBMENU_DIR:-${HOME}/Documents/git/3ds-hbmenu}"

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -d "${HBMENU_DIR}" ] || die "hbmenu checkout not found: ${HBMENU_DIR}"

# A shallow clone has no tags, so the Makefile's `git describe` fails and the
# version string comes out empty. Supply one rather than let it error.
VERSTRING="${VERSTRING:-a11y}"

if [ "${1:-}" = "--clean" ]; then
    log "cleaning"
    make -C "${HBMENU_DIR}" clean VERSTRING="${VERSTRING}"
    exit 0
fi

# The texture atlas rule does not resolve its input through VPATH correctly when
# invoked indirectly, so build it up front from the repo root where it works.
if [ ! -f "${HBMENU_DIR}/romfs/gfx/images.t3x" ]; then
    log "building texture atlas"
    mkdir -p "${HBMENU_DIR}/romfs/gfx" "${HBMENU_DIR}/build"
    ( cd "${HBMENU_DIR}" && tex3ds -i gfx/images.t3s \
        -H build/images.h -o romfs/gfx/images.t3x )
fi

log "hbmenu"
make -C "${HBMENU_DIR}" VERSTRING="${VERSTRING}"

echo
log "built:"
ls -la "${HBMENU_DIR}"/*.3dsx 2>/dev/null || true
