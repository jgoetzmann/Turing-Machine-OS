#!/usr/bin/env sh
set -eu

OUT="$(printf 'halt\n' | ./build/turingos 2>&1)"

case "$OUT" in *"HALT"*) ;; *)
    echo "FAIL: expected HALT message from shell halt command" >&2
    exit 1
    ;;
esac
case "$OUT" in *"reason=COMMAND"*) ;; *)
    echo "FAIL: expected kernel halt (state=5) after shell halt" >&2
    exit 1
    ;;
esac

echo "PASS: test_halt_command"
