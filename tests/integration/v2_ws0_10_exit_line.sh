#!/usr/bin/env sh
# WS0-10: EOF on stdin prints the prompt and the exit line with reason=EOF.
set -u
cd "$(dirname "$0")/../.." || exit 1
out=$(./build/turingos --disk=build/disk/disk.img </dev/null 2>&1)
case "$out" in *"A> "*) ;; *) echo "FAIL: WS0-10 no prompt"; exit 1 ;; esac
echo "$out" | tail -1 | grep -Eq '^TuringOS halted \(reason=EOF\) after [0-9]+ steps$' || { echo "FAIL: WS0-10 exit line: $(echo "$out" | tail -1)"; exit 1; }
case "$out" in *"stub boot"*) echo "FAIL: WS0-10 stub line still printed"; exit 1 ;; esac
echo "PASS: v2_ws0_10_exit_line"
