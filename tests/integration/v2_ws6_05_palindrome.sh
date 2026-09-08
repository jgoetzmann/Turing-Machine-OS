#!/usr/bin/env sh
# WS6-05: pal1.tm (1 tape) and pal2.tm (2 tapes) answer yes for abba and no for abca.
set -u
cd "$(dirname "$0")/../.." || exit 1
for m in pal1 pal2; do
  for case in "abba yes" "abca no"; do
    w=${case% *}; want=${case#* }
    sed "s/^input:.*/input: $w/" "demos/tm/$m.tm" > "build/tests/${m}_$w.tm"
    ./build/tmc "build/tests/${m}_$w.tm" "build/tests/${m}_$w.com" || { echo "FAIL: WS6-05 tmc $m $w"; exit 1; }
    ./build/mkdisk "build/tests/pal.img" --format --add "build/tests/${m}_$w.com:P.COM" >/dev/null
    out=$(printf 'run P.COM\nhalt\n' | ./build/turingos --disk=build/tests/pal.img --tapes=2 2>&1 | sed -n '1p' | sed 's/^A> //')
    [ "$out" = "$want" ] || { echo "FAIL: WS6-05 $m $w: got '$out' want '$want'"; exit 1; }
    ref=$(python3 tools/tm_ref.py "build/tests/${m}_$w.tm" | head -1)
    [ "$ref" = "$want" ] || { echo "FAIL: WS6-05 tm_ref $m $w: got '$ref'"; exit 1; }
  done
done
echo "PASS: v2_ws6_05_palindrome"
