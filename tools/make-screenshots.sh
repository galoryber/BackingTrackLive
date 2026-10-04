#!/usr/bin/env sh
# Regenerate docs/images/ from the real UI.
#
# Screenshots in a README rot quietly: the program changes, the picture does
# not, and nobody notices until someone downloads it and finds a different
# program. These are rendered from the actual binary through the offscreen
# path CI already uses, so regenerating them is a command rather than a chore.
#
# Needs a Windows build of BackingTrackLive.exe and raw2png.py. Run from the
# repository root:
#
#   tools/make-screenshots.sh <path-to-BackingTrackLive.exe> <path-to-raw2png.py>
set -eu

EXE=${1:?usage: make-screenshots.sh <BackingTrackLive.exe> <raw2png.py>}
RAW2PNG=${2:?usage: make-screenshots.sh <BackingTrackLive.exe> <raw2png.py>}
W=1440
H=810
OUT=docs/images

mkdir -p "$OUT"
for state in stopped playing editalign editsong editaudio armed; do
    "$EXE" --shot "shot_$state.raw" --w "$W" --h "$H" --state "$state"
    python3 "$RAW2PNG" "shot_$state.raw" "$OUT/$state.png" "$W" "$H"
    rm -f "shot_$state.raw"
    echo "  $OUT/$state.png"
done
echo "done - check them before committing; a stale screenshot is worse than none"
