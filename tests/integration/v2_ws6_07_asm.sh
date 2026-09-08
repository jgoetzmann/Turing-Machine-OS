#!/usr/bin/env sh
# WS6-07: asm hello.asm runs and prints; disasm output reassembles byte-identically; asm works inside the OS.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/$(basename "$0" .sh).img; cp build/disk/demo.img "$img"   # never mutate the shared demo disk
./build/asm demos/asm/hello.asm build/tests/hello_asm.com || { echo "FAIL: WS6-07 asm"; exit 1; }
./build/mkdisk build/tests/asm.img --format --add build/tests/hello_asm.com:HELLO.COM >/dev/null
out=$(printf 'run HELLO.COM\nhalt\n' | ./build/turingos --disk=build/tests/asm.img 2>&1)
case "$out" in *"HELLO FROM ASM"*) ;; *) echo "FAIL: WS6-07 run output"; echo "$out" | head -3; exit 1 ;; esac
./build/disasm build/tests/hello_asm.com | sed -E 's/^[0-9A-F]{4}: ([0-9A-F]{2} ?){1,3} +//' > build/tests/hello_re.asm
./build/asm build/tests/hello_re.asm build/tests/hello_re.com || { echo "FAIL: WS6-07 reassemble"; head -5 build/tests/hello_re.asm; exit 1; }
cmp -s build/tests/hello_re.com build/tests/hello_asm.com || { echo "FAIL: WS6-07 disasm round trip differs"; exit 1; }
out=$(printf 'asm HELLO.ASM\nrun HELLO.COM\nhalt\n' | ./build/turingos --disk="$img" 2>&1)
case "$out" in *"HELLO FROM ASM"*) ;; *) echo "FAIL: WS6-07 asm inside the OS"; exit 1 ;; esac
# WS1-08 / WS7-01: the shell itself survives disassemble -> reassemble byte for byte.
./build/disasm build/bin/shell.com | sed -E 's/^[0-9A-F]{4}: ([0-9A-F]{2} ?){1,3} +//' > build/tests/shell_re.asm
./build/asm build/tests/shell_re.asm build/tests/shell_re.com || { echo "FAIL: WS1-08 reassembling shell.com"; exit 1; }
cmp -s build/tests/shell_re.com build/bin/shell.com || { echo "FAIL: WS1-08 shell.com round trip differs"; exit 1; }
echo "PASS: v2_ws6_07_asm"
