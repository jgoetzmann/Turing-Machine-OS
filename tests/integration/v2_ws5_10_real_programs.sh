#!/usr/bin/env sh
# WS5-10: the hello demos compute their answers instead of printing them.
set -u
cd "$(dirname "$0")/../.." || exit 1
grep -q 'print_int' demos/hello/add.c || { echo "FAIL: WS5-10 add.c has no print_int"; exit 1; }
grep -q '\[' demos/hello/memtest.c || { echo "FAIL: WS5-10 memtest.c uses no array"; exit 1; }
for f in add strcat memtest count; do
  exp=$(head -1 "demos/hello/$f.expected")
  grep -Fq "puts(\"$exp\")" "demos/hello/$f.c" && { echo "FAIL: WS5-10 $f.c prints its answer literally"; exit 1; }
done
[ ! -d tests/compiler/programs ] || { echo "FAIL: WS5-10 placeholder programs still present"; exit 1; }
echo "PASS: v2_ws5_10_real_programs"
