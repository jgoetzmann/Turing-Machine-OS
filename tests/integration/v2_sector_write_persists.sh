#!/usr/bin/env sh
# A sector written by a program reaches the host's disk image. The BIOS marks the image dirty and
# the CLI writes it out once on the way out (src/main.c), rather than rewriting half a megabyte per
# sector: this checks the bytes are really there afterwards.
set -eu
cd "$(dirname "$0")/../.." || exit 1

OUT=build/tests
IMG=$OUT/sector_write.img
SRC=$OUT/sector_write.asm
COM=$OUT/sector_write.com
mkdir -p "$OUT"

cat > "$SRC" <<'ASM'
        ORG 0100H
        LXI D,4000H     ; DMA buffer
        MVI A,0CH
        OUT 1           ; SETDMA
        MVI C,10
        MVI A,0AH
        OUT 1           ; SETTRK 10
        MVI C,3
        MVI A,0BH
        OUT 1           ; SETSEC 3
        LXI H,4000H     ; fill the buffer with 'Z'
        MVI B,0
FILL:   MVI M,5AH
        INX H
        DCR B
        JNZ FILL
        MVI A,0EH
        OUT 1           ; WRITE
        HLT
ASM

./build/asm "$SRC" "$COM" > /dev/null
./build/mkdisk "$IMG" --format > /dev/null
./build/mkdisk "$IMG" --add "$COM:SW.COM" > /dev/null

printf 'run SW.COM\nhalt\n' | ./build/turingos --disk="$IMG" > /dev/null

# track 10, sector 3 -> ((10 * 26) + 2) * 256 = 67072
got=$(od -An -tx1 -j 67072 -N 8 "$IMG" | tr -d ' \n')
[ "$got" = "5a5a5a5a5a5a5a5a" ] || {
  echo "FAIL: v2_sector_write_persists: sector 10/3 holds '$got', expected eight 5a bytes"
  exit 1
}
echo "PASS: v2_sector_write_persists"
