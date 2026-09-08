#!/usr/bin/env sh
# WS0-09 / WS8-01: CI runs make test on ubuntu + macos with a sanitizer job; Pages deploys web/dist with deploy-pages.
set -u
cd "$(dirname "$0")/../.." || exit 1
f=.github/workflows/ci.yml
grep -q 'ubuntu-latest' $f && grep -q 'macos-latest' $f && grep -q 'make test' $f && grep -q 'fsanitize=address,undefined' $f || { echo "FAIL: WS0-09 ci.yml"; exit 1; }
p=.github/workflows/pages.yml
grep -q 'actions/deploy-pages' $p && grep -q 'web/dist' $p && grep -q 'make web' $p || { echo "FAIL: WS8-01 pages.yml"; exit 1; }
grep -q "base: '/Turing-Machine-OS/'" web/vite.config.ts || { echo "FAIL: WS8-01 vite base"; exit 1; }
grep -q '6.0.9' web/emsdk-version.txt || { echo "FAIL: WS8-01 emsdk pin"; exit 1; }
echo "PASS: v2_ws0_09_ci"
