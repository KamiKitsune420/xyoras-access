#!/usr/bin/env bash
# Ensures the calling script is running inside devkitPro's own MSYS2 shell,
# re-executing itself there if it is not. Source this FIRST, before env.sh:
#
#   source "$(dirname "${BASH_SOURCE[0]}")/msys-guard.sh"
#
# Why this is necessary
# ---------------------
# devkitPro ships its own MSYS2 (make, sed, coreutils) linked against its own
# msys-2.0.dll. Git Bash ships a different one. Run devkitPro's make from Git
# Bash and three things break at once:
#
#   1. Exported environment variables are not inherited, so every devkitPro
#      Makefile halts at "Please set DEVKITARM in your environment".
#   2. Sub-makes spawn Git Bash, which has no /opt/devkitpro mount, so paths
#      inside the recipes resolve to nothing.
#   3. PATH does not carry across, so arm-none-eabi-gcc is "command not found"
#      even when it is plainly on PATH in the parent shell.
#
# Passing variables on the make command line papers over (1) but not (2) or
# (3). Re-executing in the right shell fixes all three properly.
#
# Also note: devkitPro's build rules do not quote paths, so the project must
# live somewhere without spaces in it. See "AI docks/07-build-environment.md".

if [ ! -d /opt/devkitpro ]; then

    _dkp_bash=""
    for _candidate in \
        "/c/devkitPro/msys2/usr/bin/bash.exe" \
        "C:/devkitPro/msys2/usr/bin/bash.exe" \
        "${DEVKITPRO:-}/msys2/usr/bin/bash.exe"
    do
        if [ -x "${_candidate}" ]; then
            _dkp_bash="${_candidate}"
            break
        fi
    done

    if [ -n "${_dkp_bash}" ]; then
        # Re-exec with an ABSOLUTE script path. $0 may well be relative
        # ("scripts/build-plugin.sh"), and -l starts a login shell that lands
        # in the home directory, so a relative path would not resolve.
        # SCRIPT_DIR is set by the caller before it sources this file, and the
        # /c/... form it holds is valid in both shells.
        _dkp_script="${SCRIPT_DIR}/$(basename "$0")"

        # -l gives us MSYS2's own profile, /opt/devkitpro mount included.
        exec "${_dkp_bash}" -l "${_dkp_script}" "$@"
    fi

    # No devkitPro MSYS2 found. Carry on and let the build fail with its own
    # error, which will be more specific than anything we could say here.
    printf '\033[1;33m!!!\033[0m devkitPro MSYS2 not found; building in the current shell may fail\n' >&2
fi

# Recipes shell out to sed and gcc, both of which need somewhere writable.
# Without this they try C:\WINDOWS and fail with a permission error.
export TMPDIR="${TMPDIR:-/tmp}"
