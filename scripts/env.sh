#!/usr/bin/env bash
# Sets up the toolchain environment for XYORAS Access.
#
#   source scripts/env.sh
#
# Works from both devkitPro MSYS2 (/opt/devkitpro) and Git Bash (/c/devkitPro).
# Safe to source repeatedly.

# Not using `set -e` — this file is sourced, and killing the user's shell on a
# failed probe is rude.

# ---- Locate devkitPro -------------------------------------------------------

if [ -z "${DEVKITPRO:-}" ] || [ ! -d "${DEVKITPRO}" ]; then
    for candidate in /opt/devkitpro /c/devkitPro "C:/devkitPro"; do
        if [ -d "$candidate/devkitARM" ]; then
            export DEVKITPRO="$candidate"
            break
        fi
    done
fi

if [ -z "${DEVKITPRO:-}" ] || [ ! -d "${DEVKITPRO}/devkitARM" ]; then
    echo "env.sh: could not find devkitPro. Install it, or set DEVKITPRO." >&2
    return 1 2>/dev/null || exit 1
fi

export DEVKITARM="${DEVKITPRO}/devkitARM"
export CTRULIB="${DEVKITPRO}/libctru"
export PORTLIBS="${DEVKITPRO}/portlibs/3ds"

# ---- PATH -------------------------------------------------------------------

_xyoras_prepend_path() {
    case ":${PATH}:" in
        *":$1:"*) ;;
        *) PATH="$1:${PATH}" ;;
    esac
}

_xyoras_prepend_path "${DEVKITARM}/bin"
_xyoras_prepend_path "${DEVKITPRO}/tools/bin"
[ -d "${PORTLIBS}/bin" ] && _xyoras_prepend_path "${PORTLIBS}/bin"
export PATH

# ---- Project paths ----------------------------------------------------------

XYORAS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export XYORAS_ROOT
export XYORAS_THIRD_PARTY="${XYORAS_ROOT}/third_party"
export XYORAS_DIST="${XYORAS_ROOT}/dist"

# Where the eSpeak NG source lives. Bootstrap fetches it into third_party/,
# but an existing local clone is preferred if one is present — override with
# ESPEAK_NG_DIR to point somewhere else.
if [ -z "${ESPEAK_NG_DIR:-}" ]; then
    export ESPEAK_NG_DIR="${XYORAS_THIRD_PARTY}/espeak-ng"
fi

# ---- make wrapper -----------------------------------------------------------
#
# devkitPro ships its own MSYS2 make. Run from Git Bash, that make links a
# different MSYS runtime than the shell does, and it does NOT inherit exported
# environment variables — every devkitPro Makefile then stops at
# "Please set DEVKITARM in your environment", despite it being set.
#
# Passing the variables as make command-line arguments works from any shell,
# so all our scripts go through this instead of calling make directly.
dkp_make() {
    make DEVKITPRO="${DEVKITPRO}" DEVKITARM="${DEVKITARM}" "$@"
}

# Note on MSYS2_ARG_CONV_EXCL: it is tempting to set this to "*" so MSYS stops
# rewriting arguments that look like Unix paths. Don't. It also stops the
# conversion native Windows tools (git, cmake, 3gxtool) actually need, and they
# then fail with "no such file" on perfectly good /c/... paths. If a specific
# tool needs an exclusion, scope it to that call.

# ---- Report -----------------------------------------------------------------

if [ "${XYORAS_ENV_QUIET:-0}" != "1" ]; then
    echo "XYORAS Access environment"
    echo "  DEVKITPRO   ${DEVKITPRO}"
    echo "  DEVKITARM   ${DEVKITARM}"
    echo "  root        ${XYORAS_ROOT}"
    if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        echo "  compiler    $(arm-none-eabi-gcc -dumpversion)"
    else
        echo "  compiler    NOT FOUND — check the devkitARM install"
    fi
fi

unset -f _xyoras_prepend_path
