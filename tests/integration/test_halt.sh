#!/usr/bin/env sh
set -eu

OUT="$(./build/turingos </dev/null)"

case "$OUT" in
  *"reason=EOF"*) ;;
  *)
    echo "FAIL: halt state not observed" >&2
    exit 1
    ;;
esac

echo "PASS: test_halt"
