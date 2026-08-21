#!/usr/bin/env bash
# Builds and runs the host tests.
#
#   scripts/host-test.sh
#
# These compile the plugin's own logic natively and run it on this machine.
# They are the only automated testing available: the plugin itself cannot run
# in an emulator (see "AI docks/10-testing-and-qa.md"), so anything not covered
# here has to wait for hardware.
#
# Uses MSVC on Windows, or gcc/clang elsewhere. Visual Studio does not need to
# be on PATH -- vswhere locates it.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TESTS_DIR="${ROOT}/tools/host-test"
OUT_DIR="${ROOT}/dist/host-test"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; }

mkdir -p "${OUT_DIR}"

# Each test names the plugin sources it needs compiled alongside it, so the
# tests exercise the shipped implementation rather than a copy of it.
TESTS=(test_queue test_wav test_bcwav test_pk6 test_names test_memchain test_phrases test_vtscan test_textbox)
SRCS_test_queue="${ROOT}/plugin/source/speech/queue.cpp"
SRCS_test_wav=""
SRCS_test_bcwav=""
SRCS_test_pk6=""
SRCS_test_memchain=""
SRCS_test_phrases=""
SRCS_test_vtscan=""
SRCS_test_textbox=""
SRCS_test_names="${ROOT}/plugin/source/data/names_species.cpp ${ROOT}/plugin/source/data/names_moves.cpp ${ROOT}/plugin/source/data/names_abilities.cpp ${ROOT}/plugin/source/data/names_items.cpp"

INCLUDES="${ROOT}/plugin/include"

# -----------------------------------------------------------------------------
# Find a host compiler
# -----------------------------------------------------------------------------

MSVC_ENV=""
if command -v cl >/dev/null 2>&1; then
    COMPILER="cl-on-path"
elif command -v g++ >/dev/null 2>&1; then
    COMPILER="gcc"
elif command -v clang++ >/dev/null 2>&1; then
    COMPILER="clang"
else
    # Visual Studio is commonly installed but not on PATH. vswhere is placed at
    # a fixed location by the VS installer precisely so it can be found.
    VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    if [ -x "${VSWHERE}" ]; then
        VS_PATH="$("${VSWHERE}" -latest -products '*' \
                    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
                    -property installationPath 2>/dev/null | tr -d '\r')"
        [ -z "${VS_PATH}" ] && VS_PATH="$("${VSWHERE}" -latest -products '*' \
                    -property installationPath 2>/dev/null | tr -d '\r')"

        if [ -n "${VS_PATH}" ] && [ -f "${VS_PATH}\\VC\\Auxiliary\\Build\\vcvars64.bat" ]; then
            MSVC_ENV="${VS_PATH}\\VC\\Auxiliary\\Build\\vcvars64.bat"
            COMPILER="msvc"
        fi
    fi
fi

if [ -z "${COMPILER:-}" ]; then
    fail "no host C++ compiler found (looked for cl, g++, clang++, and Visual Studio)"
    exit 1
fi

log "compiler: ${COMPILER}"

# -----------------------------------------------------------------------------
# Build and run
# -----------------------------------------------------------------------------

failed=0
ran=0

for t in "${TESTS[@]}"; do
    src="${TESTS_DIR}/${t}.cpp"
    exe="${OUT_DIR}/${t}.exe"

    [ -f "${src}" ] || { fail "missing ${src}"; failed=1; continue; }

    # Indirect lookup of the per-test source list, without bash 4 namerefs.
    eval "extra_srcs=\${SRCS_${t}}"

    log "building ${t}"

    case "${COMPILER}" in
        msvc)
            # Quoting a vcvars call plus a cl invocation through `cmd /c` from
            # bash is a losing battle: the quotes arrive mangled and cmd reports
            # the batch file "is not recognized". Writing a throwaway .bat
            # sidesteps the escaping entirely.
            #
            # /EHsc for standard exception semantics: the plugin itself builds
            # without exceptions, but the host build uses std::mutex, which
            # wants them.
            bat="${OUT_DIR}/build_${t}.bat"
            {
                echo "@echo off"
                echo "call \"${MSVC_ENV}\" >nul"
                win_srcs="\"$(cygpath -w "${src}")\""
                for extra in ${extra_srcs}; do
                    win_srcs="${win_srcs} \"$(cygpath -w "${extra}")\""
                done
                echo "cl /nologo /EHsc /W3 /std:c++17 /I \"$(cygpath -w "${INCLUDES}")\" /Fe:\"$(cygpath -w "${exe}")\" /Fo:\"$(cygpath -w "${OUT_DIR}")\\\\\" ${win_srcs}"
            } > "${bat}"

            cmd //c "$(cygpath -w "${bat}")" 2>&1 \
                | grep -vE "^Microsoft \(R\)|^Copyright \(C\)|^$|^[A-Za-z_]+\.cpp$" | head -25
            ;;
        cl-on-path)
            cl //nologo //EHsc //W3 //std:c++17 //I "${INCLUDES}" //Fe:"${exe}" "${src}" ${extra_srcs} 2>&1 | head -25
            ;;
        gcc)
            g++ -std=c++11 -Wall -Wextra -pthread -I "${INCLUDES}" -o "${exe}" "${src}" ${extra_srcs} 2>&1 | head -25
            ;;
        clang)
            clang++ -std=c++11 -Wall -Wextra -pthread -I "${INCLUDES}" -o "${exe}" "${src}" ${extra_srcs} 2>&1 | head -25
            ;;
    esac

    if [ ! -f "${exe}" ]; then
        fail "${t} failed to build"
        failed=1
        continue
    fi

    "${exe}"
    rc=$?
    ran=$((ran + 1))
    [ ${rc} -ne 0 ] && failed=1
done

echo
if [ ${failed} -eq 0 ]; then
    printf '\033[1;32m==>\033[0m all %d host test suites passed\n' "${ran}"
else
    printf '\033[1;31m==>\033[0m host tests FAILED\n'
fi

exit ${failed}
