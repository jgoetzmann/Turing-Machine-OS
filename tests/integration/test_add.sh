#!/usr/bin/env sh
set -eu

CC_CASE=add ./build/tests/compiler_test_expected_outputs >/dev/null
echo "PASS: test_add"
