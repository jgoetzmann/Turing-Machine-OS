#!/usr/bin/env sh
# WS6-06: cc SHELL.C inside the OS reproduces build/bin/shell.com byte for byte.
set -u
cd "$(dirname "$0")/../.." || exit 1
cp build/disk/demo.img build/tests/selfc.img
printf 'cc SHELL.C\nhalt\n' | ./build/turingos --disk=build/tests/selfc.img >/dev/null 2>&1
./build/mkdisk build/tests/selfc.img --extract SHELL.COM build/tests/shell_self.com >/dev/null || { echo "FAIL: WS6-06 SHELL.COM not produced"; exit 1; }
cmp -s build/tests/shell_self.com build/bin/shell.com || { echo "FAIL: WS6-06 SHELL.COM differs from build/bin/shell.com"; exit 1; }
echo "PASS: v2_ws6_06_self_compile"
