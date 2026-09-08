#!/usr/bin/env sh
# WS1-11a: mkdisk --add / --ls / --extract round-trip and :NAME.EXT renaming.
set -u
cd "$(dirname "$0")/../.." || exit 1
img=build/tests/mk.img; rm -f "$img"
./build/mkdisk "$img" --add demos/hello/add.c >/dev/null || { echo "FAIL: WS1-11a add"; exit 1; }
./build/mkdisk "$img" --ls | grep -q '^ADD.C' || { echo "FAIL: WS1-11a ls"; ./build/mkdisk "$img" --ls; exit 1; }
./build/mkdisk "$img" --extract ADD.C build/tests/add_out.c >/dev/null || { echo "FAIL: WS1-11a extract"; exit 1; }
cmp -s build/tests/add_out.c demos/hello/add.c || { echo "FAIL: WS1-11a extracted bytes differ"; exit 1; }
./build/mkdisk "$img" --add demos/hello/hello.c:HI.C >/dev/null && ./build/mkdisk "$img" --ls | grep -q '^HI.C' || { echo "FAIL: WS1-11a rename"; exit 1; }
[ "$(wc -c < "$img" | tr -d ' ')" = 512512 ] || { echo "FAIL: WS1-11a image size"; exit 1; }
echo "PASS: v2_ws1_11a_mkdisk"
