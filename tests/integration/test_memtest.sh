#!/usr/bin/env sh
set -eu

CC_CASE=memtest ./build/tests/compiler_test_expected_outputs >/dev/null
echo "PASS: test_memtest"
