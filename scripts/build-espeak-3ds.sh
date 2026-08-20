#!/usr/bin/env bash
# Cross-compiles eSpeak NG to a static library for the 3DS (ARM11).
#
#   scripts/build-espeak-3ds.sh [--clean]
#
# Output:
#   third_party/espeak-ng/build-3ds/src/libespeak-ng/libespeak-ng.a
#   dist/sdcard/xyoras-access/espeak-ng-data/   (trimmed English voice data)
#
# The option set mirrors the known-good devkitPPC Wii build of the same
# library; see "AI docks/06-tts-audio-pipeline.md" for why each is set.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Re-exec inside devkitPro MSYS2 if needed; see msys-guard.sh for why.
# shellcheck source=msys-guard.sh
source "${SCRIPT_DIR}/msys-guard.sh"
# shellcheck source=env.sh
XYORAS_ENV_QUIET=1 source "${SCRIPT_DIR}/env.sh"

BUILD_DIR="${ESPEAK_NG_DIR}/build-3ds"
TOOLCHAIN="${XYORAS_ROOT}/cmake/3DSToolchain.cmake"
SD_DATA_DIR="${XYORAS_DIST}/sdcard/xyoras-access/espeak-ng-data"

log() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

[ -f "${ESPEAK_NG_DIR}/CMakeLists.txt" ] \
    || die "eSpeak NG source not found at ${ESPEAK_NG_DIR}. Run scripts/bootstrap.sh first."

if [ "${1:-}" = "--clean" ]; then
    log "removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

# -----------------------------------------------------------------------------
# Configure
# -----------------------------------------------------------------------------

log "configuring eSpeak NG for ARM11"
cmake -S "${ESPEAK_NG_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_TESTS=OFF \
    -DCOMPILE_INTONATIONS=OFF \
    -DUSE_MBROLA=OFF \
    -DUSE_ASYNC=OFF \
    -DUSE_LIBPCAUDIO=OFF \
    -DUSE_LIBSONIC=OFF \
    -DUSE_KLATT=ON \
    -DEXTRA_cmn=OFF \
    -DEXTRA_ru=OFF

# -----------------------------------------------------------------------------
# Build the library only
# -----------------------------------------------------------------------------
#
# Only the `espeak-ng` library target. The CLI and the test binaries are host
# programs and cannot link for this target.

log "building libespeak-ng.a"
cmake --build "${BUILD_DIR}" --target espeak-ng -- -j"$(nproc 2>/dev/null || echo 4)"

LIB="${BUILD_DIR}/src/libespeak-ng/libespeak-ng.a"
[ -f "${LIB}" ] || LIB="$(find "${BUILD_DIR}" -name 'libespeak-ng.a' -print -quit)"
[ -n "${LIB}" ] && [ -f "${LIB}" ] || die "build finished but libespeak-ng.a was not produced"

log "built $(basename "${LIB}") ($(du -h "${LIB}" | cut -f1))"

# -----------------------------------------------------------------------------
# Voice data
# -----------------------------------------------------------------------------
#
# eSpeak's phoneme and dictionary data is COMPILED by host tools during a
# normal build. We are cross-compiling, so those tools never run for our
# target, and the source tree carries only the uncompiled sources for them.
#
# The compiled files therefore come from an eSpeak NG installation of the SAME
# VERSION -- which is exactly why bootstrap.sh pins the source to a release
# tag. A library and a data set from different versions will not load.
#
# Set ESPEAK_SYSTEM_DATA to override the search.

log "staging English voice data"

ESPEAK_VERSION="$(cat "${XYORAS_ROOT}/.espeak-version" 2>/dev/null || echo '1.52.0')"

SRC_DATA=""
for d in \
    "${ESPEAK_SYSTEM_DATA:-}" \
    "${BUILD_DIR}/espeak-ng-data" \
    "/c/Program Files/eSpeak NG/espeak-ng-data" \
    "/usr/share/espeak-ng-data" \
    "/usr/lib/x86_64-linux-gnu/espeak-ng-data" \
    "${ESPEAK_NG_DIR}/espeak-ng-data"
do
    # phondata is the expensive one to generate. If it is present, this is a
    # real compiled data set rather than a stub directory.
    if [ -n "${d}" ] && [ -f "${d}/phondata" ]; then
        SRC_DATA="${d}"
        break
    fi
done

if [ -z "${SRC_DATA}" ]; then
    printf '\033[1;33m!!!\033[0m no compiled espeak-ng-data found -- the mod would be mute.\n' >&2
    printf '    Install eSpeak NG %s, or set ESPEAK_SYSTEM_DATA to a directory\n' "${ESPEAK_VERSION}" >&2
    printf '    containing phondata, phontab, phonindex and en_dict.\n' >&2
else
    log "voice data source: ${SRC_DATA}"

    rm -rf "${SD_DATA_DIR}"
    mkdir -p "${SD_DATA_DIR}/lang/gmw" "${SD_DATA_DIR}/voices/!v"

    # Only what English needs. The full set covers 100+ languages and is many
    # times larger for no benefit to us.
    missing=0
    for f in phontab phonindex phondata phondata-manifest intonations en_dict; do
        if [ -f "${SRC_DATA}/${f}" ]; then
            cp "${SRC_DATA}/${f}" "${SD_DATA_DIR}/"
        else
            printf '\033[1;33m!!!\033[0m voice data missing: %s\n' "${f}" >&2
            missing=1
        fi
    done

    if [ -f "${SRC_DATA}/lang/gmw/en" ]; then
        cp "${SRC_DATA}/lang/gmw/en" "${SD_DATA_DIR}/lang/gmw/"
    else
        printf '\033[1;33m!!!\033[0m voice data missing: lang/gmw/en\n' >&2
        missing=1
    fi

    # Voice variants are tiny and let the player change how the voice sounds.
    if [ -d "${SRC_DATA}/voices/!v" ]; then
        cp -r "${SRC_DATA}/voices/!v/." "${SD_DATA_DIR}/voices/!v/" 2>/dev/null || true
    fi

    if [ "${missing}" -ne 0 ]; then
        printf '\033[1;33m!!!\033[0m voice data is incomplete; speech may fail to start\n' >&2
    fi

    log "voice data staged at ${SD_DATA_DIR} ($(du -sh "${SD_DATA_DIR}" | cut -f1))"
fi

log "done — next: scripts/build-plugin.sh"
