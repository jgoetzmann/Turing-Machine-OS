#!/usr/bin/env sh
# WS7-04: the Forth REPL compiled and run inside the OS answers ": sq dup * ; 7 sq ." with 49.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/$(basename "$0" .sh).img; cp build/disk/demo.img "$img"   # never mutate the shared demo disk
out=$(printf 'cc FORTH.C\nrun FORTH.COM\n: sq dup * ;\n7 sq .\nbye\nhalt\n' | ./build/turingos --disk="$img" 2>&1)
case "$out" in *"ok
ok
49 ok"*"reason=COMMAND"*) ;; *) echo "FAIL: WS7-04"; echo "$out" | head -8; exit 1 ;; esac
echo "PASS: v2_ws7_04_forth"
