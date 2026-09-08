#!/usr/bin/env sh
# WS0-11: make help lists every target named in SPEC S8.
set -u
cd "$(dirname "$0")/../.." || exit 1
out=$(make help 2>&1)
for t in all tools shell gen disk demo-disk test wasm web test-web run bench disasm clean help; do
  echo "$out" | grep -q "^$t:" || { echo "FAIL: WS0-11 make help lacks target '$t'"; exit 1; }
done
echo "PASS: v2_ws0_11_make_help"
