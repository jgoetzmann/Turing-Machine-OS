#!/usr/bin/env sh
# WS9-01: the pygame visualizer is gone.
set -u
cd "$(dirname "$0")/../.." || exit 1
[ ! -d viz ] || { echo "FAIL: WS9-01 viz/ exists"; exit 1; }
hits=$(git grep -il pygame -- . ':!docs/decisions.md' ':!docs/v2-roadmap.md' ':!docs/how-it-was-built.md' ':!tests/repo/v2_ws9_01_no_pygame.sh' 2>/dev/null)
[ -z "$hits" ] || { echo "FAIL: WS9-01 pygame mentioned in: $hits"; exit 1; }
grep -q 'visualizer' docker-compose.yml && { echo "FAIL: WS9-01 docker-compose still has a visualizer service"; exit 1; }
grep -q 'python3-pygame' Dockerfile && { echo "FAIL: WS9-01 Dockerfile installs pygame"; exit 1; }
echo "PASS: v2_ws9_01_no_pygame"
