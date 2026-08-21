#!/usr/bin/env bash
# Plays the speech the plugin produced, through the PC.
#
#   scripts/play-speech.sh              everything from the last emulator run
#   scripts/play-speech.sh 2            just utterance 0002
#   scripts/play-speech.sh /some/dir    a directory of .wav files
#
# The emulator cannot play the mod's audio -- no 3DS emulator implements CSND.
# The plugin's WAV backend writes each utterance to the SD card instead, and
# this plays those files, in order, through Windows.
#
# This is the whole point of that backend: without it there is no way to hear
# what the mod says until it runs on a console.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; exit 1; }

# shellcheck source=/dev/null
[ -f "${SCRIPT_DIR}/env.local.sh" ] && source "${SCRIPT_DIR}/env.local.sh"

USERDIR="${XYORAS_AZAHAR_USER:-${APPDATA:-${HOME}/AppData/Roaming}/Azahar}"
DEFAULT_DIR="${USERDIR}/sdmc/xyoras-access/speech"

ARG="${1:-}"
ONLY=""

if [ -z "${ARG}" ]; then
    DIR="${DEFAULT_DIR}"
elif [ -d "${ARG}" ]; then
    DIR="${ARG}"
else
    # A bare number selects one utterance.
    DIR="${DEFAULT_DIR}"
    ONLY="$(printf '%04d' "${ARG}" 2>/dev/null)" || die "not a directory or a number: ${ARG}"
fi

[ -d "${DIR}" ] || die "no speech directory: ${DIR}
Run scripts/run-emulator.sh first."

if [ -n "${ONLY}" ]; then
    FILES=("${DIR}/${ONLY}.wav")
    [ -f "${FILES[0]}" ] || die "no such utterance: ${FILES[0]}"
else
    mapfile -t FILES < <(find "${DIR}" -maxdepth 1 -name '*.wav' | sort)
    [ "${#FILES[@]}" -gt 0 ] || die "no .wav files in ${DIR}"
fi

log "playing ${#FILES[@]} utterance(s) from ${DIR}"

for f in "${FILES[@]}"; do
    # Duration, so a file that is silent or truncated is obvious before it plays.
    secs="$(python -c "
import struct,sys
d=open(sys.argv[1],'rb').read(44)
sr=struct.unpack_from('<I',d,24)[0]
ch=struct.unpack_from('<H',d,22)[0]
bits=struct.unpack_from('<H',d,34)[0]
n=struct.unpack_from('<I',d,40)[0]
print('%.1f' % (n/(sr*ch*bits/8)))
" "$f" 2>/dev/null || echo '?')"

    printf '    %s  (%ss)\n' "$(basename "$f")" "${secs}"

    if command -v cygpath >/dev/null 2>&1; then
        win="$(cygpath -w "$f")"
    else
        win="$f"
    fi

    # PlaySync so utterances play in order rather than on top of each other.
    powershell -NoProfile -NonInteractive -Command \
        "(New-Object Media.SoundPlayer '${win}').PlaySync()" </dev/null
done

log "done"
