#!/usr/bin/env sh
# WS6-08: bf demos run inside the OS on 1 and 2 tapes, back to back.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/$(basename "$0" .sh).img; cp build/disk/demo.img "$img"   # never mutate the shared demo disk
for tapes in 1 2; do
  out=$(printf 'bf HELLO.BF\nrun HELLO.COM\nbf NESTED.BF\nrun NESTED.COM\nrun HELLO.COM\nhalt\n' | ./build/turingos --disk="$img" --tapes=$tapes 2>&1)
  case "$out" in *"Hello World!"*"ABC"*"Hello World!"*) ;; *) echo "FAIL: WS6-08 tapes=$tapes"; echo "$out" | head -5; exit 1 ;; esac
done
echo "PASS: v2_ws6_08_bf"
