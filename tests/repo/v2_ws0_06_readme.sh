#!/usr/bin/env sh
# WS0-06: README links the Pages site and the quickstart.
set -u
cd "$(dirname "$0")/../.." || exit 1
grep -q 'https://jgoetzmann.github.io/Turing-Machine-OS/' README.md || { echo "FAIL: WS0-06 no Pages link"; exit 1; }
grep -q 'make test' README.md || { echo "FAIL: WS0-06 no make test"; exit 1; }
grep -q 'MIT' README.md || { echo "FAIL: WS0-06 no license"; exit 1; }
echo "PASS: v2_ws0_06_readme"
