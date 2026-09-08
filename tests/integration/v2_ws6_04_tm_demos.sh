#!/usr/bin/env sh
# WS6-04: the TM demos compiled by tmc and run inside the OS match their .expected files and tools/tm_ref.py.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/tm_demos.img; rm -f "$img"; args=""
for t in bb2 bb3 bb4 inc pal1 pal2; do
  ./build/tmc "demos/tm/$t.tm" "build/tests/$t.com" || { echo "FAIL: WS6-04 tmc $t"; exit 1; }
  args="$args --add build/tests/$t.com:$(echo $t | tr a-z A-Z).COM"
done
./build/mkdisk "$img" --format $args >/dev/null || { echo "FAIL: WS6-04 mkdisk"; exit 1; }
for t in bb2 bb3 bb4 inc pal1 pal2; do
  U=$(echo "$t" | tr a-z A-Z)
  out=$(printf 'run %s.COM\nhalt\n' "$U" | ./build/turingos --disk="$img" --tapes=2 2>&1 | sed '1s/^A> //' | sed '$d' | sed 's/A> HALT$//')
  exp=$(cat "demos/tm/$t.expected"); ref=$(python3 tools/tm_ref.py "demos/tm/$t.tm")
  [ "$out" = "$exp" ] || { echo "FAIL: WS6-04 $t: OS output differs from .expected"; printf '%s\n' "$out" | head -4; exit 1; }
  [ "$ref" = "$exp" ] || { echo "FAIL: WS6-04 $t: tm_ref.py differs from .expected"; exit 1; }
done
ones() { head -1 "demos/tm/$1.expected" | tr -cd '1' | wc -c | tr -d ' '; }
steps() { grep -o 'steps=[0-9]*' "demos/tm/$1.expected" | cut -d= -f2; }
[ "$(ones bb2)" = 4 ] && [ "$(steps bb2)" = 6 ] || { echo "FAIL: WS6-04 bb2 counts"; exit 1; }
[ "$(ones bb3)" = 6 ] && [ "$(steps bb3)" = 14 ] || { echo "FAIL: WS6-04 bb3 counts"; exit 1; }
[ "$(ones bb4)" = 13 ] && [ "$(steps bb4)" = 107 ] || { echo "FAIL: WS6-04 bb4 counts"; exit 1; }
[ "$(head -1 demos/tm/inc.expected)" = 1100 ] || { echo "FAIL: WS6-04 inc"; exit 1; }
echo "PASS: v2_ws6_04_tm_demos"
