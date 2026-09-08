#!/usr/bin/env sh
# WS1-12: cc + run every demos/hello program inside the OS from the demo disk.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/$(basename "$0" .sh).img; cp build/disk/demo.img "$img"   # never mutate the shared demo disk
fail=0
for p in add strcat count echo memtest hello; do
  U=$(echo "$p" | tr a-z A-Z)
  if [ "$p" = echo ]; then in="cc $U.C\nrun $U.COM\nhello\nhalt\n"; else in="cc $U.C\nrun $U.COM\nhalt\n"; fi
  out=$(printf "$in" | ./build/turingos --disk="$img" 2>&1)
  exp=$(cat "demos/hello/$p.expected")
  case "$out" in
    *"A> "*"$exp"*"TuringOS halted (reason=COMMAND)"*) ;;
    *) echo "FAIL: WS1-12 $p: expected [$exp] in session output"; echo "$out" | head -5; fail=1 ;;
  esac
done
# WS6-02 / WS6-03: the graphical demos compile inside the OS too (they need keys/vsync to run, so compile only).
out=$(printf 'cc PONG.C\ncc LIFE.C\ndir\nhalt\n' | ./build/turingos --disk="$img" 2>&1)
case "$out" in *"PONG.COM"*"LIFE.COM"*|*"LIFE.COM"*"PONG.COM"*) ;; *) echo "FAIL: WS6-02 cc PONG.C / LIFE.C inside the OS"; echo "$out" | head -5; fail=1 ;; esac
[ $fail -eq 0 ] && echo "PASS: v2_ws1_12_cc_run_demos"
exit $fail
