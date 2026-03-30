#!/usr/bin/env sh
set -eu

BUILD_DIR="build/tests/emu"
FS_BUILD_DIR="build/tests/fs"
TEST_BIN="${BUILD_DIR}/test_mem"
CPU_TEST_BIN="${BUILD_DIR}/test_cpu_data_transfer"
CPU_ARITH_BIN="${BUILD_DIR}/test_cpu_arithmetic"
CPU_LOGIC_BIN="${BUILD_DIR}/test_cpu_logic"
CPU_BRANCH_BIN="${BUILD_DIR}/test_cpu_branch"
CPU_STACK_BIN="${BUILD_DIR}/test_cpu_stack"
CPU_IO_BIN="${BUILD_DIR}/test_cpu_io"
CPU_CONTROL_BIN="${BUILD_DIR}/test_cpu_control"
CPU_OPCODES_BIN="${BUILD_DIR}/test_opcodes"
CPU_HLT_BIN="${BUILD_DIR}/test_cpu_hlt"
CPU_FLAGS_BIN="${BUILD_DIR}/test_cpu_flags"
BIOS_TEST_BIN="${BUILD_DIR}/test_bios"
FS_SECTOR_TEST_BIN="${FS_BUILD_DIR}/test_fs_sector"

mkdir -p "${BUILD_DIR}"
mkdir -p "${FS_BUILD_DIR}"

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

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_control.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_CONTROL_BIN}"

"${CPU_CONTROL_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_opcodes.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_OPCODES_BIN}"

"${CPU_OPCODES_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_hlt.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_HLT_BIN}"

"${CPU_HLT_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/emu/test_cpu_flags.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  -o "${CPU_FLAGS_BIN}"

"${CPU_FLAGS_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/bios/test_bios.c \
  ./src/bios/bios.c \
  ./src/emu/cpu.c \
  ./src/emu/mem.c \
  ./src/fs/fs.c \
  -o "${BIOS_TEST_BIN}"

"${BIOS_TEST_BIN}"

cc -std=c99 -Wall -Wextra -Werror -pedantic \
  -I./src \
  ./tests/fs/test_fs_sector.c \
  ./src/fs/fs.c \
  -o "${FS_SECTOR_TEST_BIN}"

"${FS_SECTOR_TEST_BIN}"

echo "All tests passed."
