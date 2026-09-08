#!/usr/bin/env sh
# WS4-01 / WS4-02: the shell and the console demos work on 1, 2 and 4 tapes and on 32K, 48K and 64K tapes.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/$(basename "$0" .sh).img
fail=0
for cfg in "--tapes=2" "--tapes=4" "--len=32768" "--len=49152" "--tapes=2 --len=32768"; do
  cp build/disk/demo.img "$img"
  out=$(printf 'cc ADD.C\nrun ADD.COM\ncc MEMTEST.C\nrun MEMTEST.COM\nbf HELLO.BF\nrun HELLO.COM\ntm BB2.TM\nrun BB2.COM\nasm HELLO.ASM\nrun HELLO.COM\nhalt\n' | ./build/turingos --disk="$img" $cfg 2>&1)
  case "$out" in
    *"3 + 4 = 7"*"sum=55"*"Hello World!"*"1111"*"steps=6"*"HELLO FROM ASM"*"reason=COMMAND"*) echo "ok   $cfg" ;;
    *) echo "FAIL: WS4-01/02 with $cfg"; echo "$out" | tail -6; fail=1 ;;
  esac
done
[ $fail -eq 0 ] && echo "PASS: v2_ws4_01_02_tapes_lengths"
exit $fail
