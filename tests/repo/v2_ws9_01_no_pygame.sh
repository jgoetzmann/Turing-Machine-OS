#!/usr/bin/env sh
# WS9-01: the pygame visualizer is gone.
# Plain grep, not `git grep`: it needs no checkout to work and it also sees untracked files.
set -u
cd "$(dirname "$0")/../.." || exit 1

[ ! -d viz ] || { echo "FAIL: WS9-01 viz/ exists"; exit 1; }

hits=$(grep -ril pygame . \
  --exclude-dir=.git --exclude-dir=build --exclude-dir=node_modules \
  --exclude-dir=dist --exclude-dir=.fullsend --exclude-dir=generated \
  --exclude=decisions.md --exclude=v2-roadmap.md --exclude=how-it-was-built.md \
  --exclude=v2_ws9_01_no_pygame.sh 2>/dev/null)
rc=$?
[ "$rc" -le 1 ] || { echo "FAIL: WS9-01 grep failed with status $rc"; exit 1; }
[ -z "$hits" ] || { echo "FAIL: WS9-01 pygame mentioned in: $hits"; exit 1; }

[ -f docker-compose.yml ] || { echo "FAIL: WS9-01 docker-compose.yml missing"; exit 1; }
[ -f Dockerfile ] || { echo "FAIL: WS9-01 Dockerfile missing"; exit 1; }
grep -q 'visualizer' docker-compose.yml && { echo "FAIL: WS9-01 docker-compose still has a visualizer service"; exit 1; }
grep -q 'python3-pygame' Dockerfile && { echo "FAIL: WS9-01 Dockerfile installs pygame"; exit 1; }

echo "PASS: v2_ws9_01_no_pygame"
