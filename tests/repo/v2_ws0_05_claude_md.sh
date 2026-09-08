#!/usr/bin/env sh
# WS0-05: CLAUDE.md exists, is at most 80 lines, forbids attribution trailers and names make test.
set -u
cd "$(dirname "$0")/../.." || exit 1
[ -f CLAUDE.md ] || { echo "FAIL: WS0-05 CLAUDE.md missing"; exit 1; }
n=$(wc -l < CLAUDE.md); [ "$n" -le 80 ] || { echo "FAIL: WS0-05 CLAUDE.md has $n lines"; exit 1; }
grep -q 'Co-Authored-By' CLAUDE.md || { echo "FAIL: WS0-05 no trailer rule"; exit 1; }
grep -q 'make test' CLAUDE.md || { echo "FAIL: WS0-05 no make test"; exit 1; }
echo "PASS: v2_ws0_05_claude_md"
