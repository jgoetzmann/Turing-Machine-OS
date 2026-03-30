#!/usr/bin/env sh
set -eu

BUILD_DIR="build/tests/emu"
TEST_BIN="${BUILD_DIR}/test_mem"
CPU_TEST_BIN="${BUILD_DIR}/test_cpu_data_transfer"
CPU_ARITH_BIN="${BUILD_DIR}/test_cpu_arithmetic"
CPU_LOGIC_BIN="${BUILD_DIR}/test_cpu_logic"
CPU_BRANCH_BIN="${BUILD_DIR}/test_cpu_branch"
CPU_STACK_BIN="${BUILD_DIR}/test_cpu_stack"
CPU_IO_BIN="${BUILD_DIR}/test_cpu_io"

mkdir -p "${BUILD_DIR}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_mem.c \
  ./src/emu/mem.c \
  -o "${TEST_BIN}"

"${TEST_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_data_transfer.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_TEST_BIN}"

"${CPU_TEST_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_arithmetic.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_ARITH_BIN}"

"${CPU_ARITH_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_logic.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_LOGIC_BIN}"

"${CPU_LOGIC_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_branch.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_BRANCH_BIN}"

"${CPU_BRANCH_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_stack.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_STACK_BIN}"

"${CPU_STACK_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_io.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_IO_BIN}"

"${CPU_IO_BIN}"

echo "All tests passed."
