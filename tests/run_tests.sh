#!/usr/bin/env sh
# Data-driven test runner: every tests/**/test_*.c is compiled standalone against build/libtos.a
# and every tests/**/*.sh (except this file) is executed. Prints PASS/FAIL per test and a summary.
set -u
cd "$(dirname "$0")/.." || exit 1
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -I./src ${EXTRA_CFLAGS:-}"
LIB=build/libtos.a
pass=0; fail=0; failed=""
run_one() { # name, command...
  name="$1"; shift
  if "$@" >"build/tests/$name.log" 2>&1; then
    pass=$((pass+1)); echo "PASS: $name"
  else
    fail=$((fail+1)); failed="$failed $name"; echo "FAIL: $name (see build/tests/$name.log)"; tail -20 "build/tests/$name.log" | sed 's/^/    /'
  fi
}
mkdir -p build/tests
for src in $(find tests -name 'test_*.c' | sort); do
  name=$(echo "$src" | sed 's#^tests/##; s#\.c$##; s#/#_#g')
  bin="build/tests/$name"
  if ${CC:-cc} $CFLAGS "$src" "$LIB" ${EXTRA_LDFLAGS:-} -o "$bin" >"build/tests/$name.log" 2>&1; then
    run_one "$name" "$bin"
  else
    fail=$((fail+1)); failed="$failed $name"; echo "FAIL: $name (compile error, see build/tests/$name.log)"; tail -20 "build/tests/$name.log" | sed 's/^/    /'
  fi
done
for sh_test in $(find tests -name '*.sh' ! -name 'run_tests.sh' | sort); do
  name=$(echo "$sh_test" | sed 's#^tests/##; s#\.sh$##; s#/#_#g')
  run_one "$name" sh "$sh_test"
done
total=$((pass+fail))
echo "$pass/$total passed"
if [ "$fail" -ne 0 ]; then echo "failed:$failed"; exit 1; fi
