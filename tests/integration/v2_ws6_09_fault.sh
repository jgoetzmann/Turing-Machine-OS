#!/usr/bin/env sh
# WS6-09: fault.c halts with reason=TAPE_FAULT on a 32K tape and returns to the shell on 64K.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/$(basename "$0" .sh).img; cp build/disk/demo.img "$img"   # never mutate the shared demo disk
out=$(printf 'cc FAULT.C\nrun FAULT.COM\nhalt\n' | ./build/turingos --disk="$img" --len=32768 2>&1)
echo "$out" | tail -1 | grep -q 'reason=TAPE_FAULT' || { echo "FAIL: WS6-09 32K: $(echo "$out" | tail -1)"; exit 1; }
out=$(printf 'cc FAULT.C\nrun FAULT.COM\nhalt\n' | ./build/turingos --disk="$img" 2>&1)
echo "$out" | tail -1 | grep -q 'reason=COMMAND' || { echo "FAIL: WS6-09 64K: $(echo "$out" | tail -1)"; exit 1; }
echo "PASS: v2_ws6_09_fault"
