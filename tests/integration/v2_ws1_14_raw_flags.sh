#!/usr/bin/env sh
# WS1-14: --raw=0 works with piped stdin; --raw=1 on a non-TTY does not crash and still halts cleanly.
set -u
cd "$(dirname "$0")/../.." || exit 1
out=$(printf 'help\nhalt\n' | ./build/turingos --disk=build/disk/disk.img --raw=0 2>&1)
case "$out" in *"dir type run cc asm tm bf del cls mem disk halt help"*"reason=COMMAND"*) ;; *) echo "FAIL: WS1-14 --raw=0"; echo "$out" | tail -3; exit 1 ;; esac
out=$(printf 'halt\n' | ./build/turingos --disk=build/disk/disk.img --raw=1 2>&1); rc=$?
[ $rc -eq 0 ] || { echo "FAIL: WS1-14 --raw=1 on a pipe exited $rc"; exit 1; }
case "$out" in *"reason=COMMAND"*) ;; *) echo "FAIL: WS1-14 --raw=1 did not halt on 'halt'"; echo "$out" | tail -3; exit 1 ;; esac
echo "PASS: v2_ws1_14_raw_flags"
