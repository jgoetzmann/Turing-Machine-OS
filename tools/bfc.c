/* tools/bfc.c — host CLI: bfc <in.bf> <out.com>
 *
 * Reads a Brainfuck source file, compiles it with bf_compile (src/lang/bf.c) into an 8080
 * .com image (ORG 0100H) and writes it. Exit 0 on success and prints nothing; on a compile
 * error prints the compiler's "line N: message" to stderr and exits 1. Usage, unreadable
 * input, oversized input and unwritable output also exit 1 with a "bfc: ..." line. */
#include <stdio.h>
#include <stdint.h>
#include "../src/tos.h"
#include "../src/lang/bf.h"

#define BFC_SRC_MAX 262144u   /* 256 KB of source is far beyond anything that fits the TPA */

static char    bfc_src[BFC_SRC_MAX + 1u];
static uint8_t bfc_out[TOS_TPA_SIZE];

int main(int argc, char **argv)
{
    FILE  *f;
    size_t n;
    size_t extra;
    char   err[256];
    int    len;

    if (argc != 3) {
        fprintf(stderr, "usage: bfc <in.bf> <out.com>\n");
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "bfc: cannot open %s\n", argv[1]);
        return 1;
    }
    n = fread(bfc_src, 1, BFC_SRC_MAX, f);
    if (ferror(f)) {
        fclose(f);
        fprintf(stderr, "bfc: read error on %s\n", argv[1]);
        return 1;
    }
    extra = fread(bfc_src + BFC_SRC_MAX, 1, 1, f);
    fclose(f);
    if (extra != 0) {
        fprintf(stderr, "bfc: %s is too large (limit %u bytes)\n", argv[1], (unsigned)BFC_SRC_MAX);
        return 1;
    }

    err[0] = 0;
    len = bf_compile(bfc_src, (uint32_t)n, bfc_out, (uint32_t)sizeof bfc_out, err, (uint32_t)sizeof err);
    if (len < 0) {
        fprintf(stderr, "%s\n", err[0] ? err : "line 1: compile error");
        return 1;
    }

    f = fopen(argv[2], "wb");
    if (!f) {
        fprintf(stderr, "bfc: cannot write %s\n", argv[2]);
        return 1;
    }
    if (fwrite(bfc_out, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        fprintf(stderr, "bfc: write error on %s\n", argv[2]);
        return 1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "bfc: write error on %s\n", argv[2]);
        return 1;
    }
    return 0;
}
