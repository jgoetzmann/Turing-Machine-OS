/* TuringOS v2 - host assembler CLI (SPEC S4, S7; behavior WS6-07).
 *
 *   asm <in.asm> <out.com>
 *
 * Reads the whole source file, runs the two-pass assembler from src/lang/asm.c and writes the
 * flat .com image. Exit 0 on success; on any error print the assembler's "line N: message"
 * text (or an "asm: ..." host message) to stderr and exit 1. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lang/asm.h"

#define SRC_MAX 32768u
#define OUT_MAX 16128u

static char    g_src[SRC_MAX + 1];
static uint8_t g_out[OUT_MAX];

int main(int argc, char **argv)
{
    FILE *f;
    size_t n;
    int len;
    char err[256];

    if (argc != 3) {
        fprintf(stderr, "usage: asm <in.asm> <out.com>\n");
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "asm: cannot open '%s'\n", argv[1]);
        return 1;
    }
    n = fread(g_src, 1, sizeof g_src, f);
    if (ferror(f)) {
        fprintf(stderr, "asm: read error on '%s'\n", argv[1]);
        fclose(f);
        return 1;
    }
    fclose(f);
    if (n > SRC_MAX) {
        fprintf(stderr, "asm: source too large (max %u bytes)\n", (unsigned)SRC_MAX);
        return 1;
    }
    g_src[n] = 0;

    err[0] = 0;
    len = asm_assemble(g_src, (uint32_t)n, g_out, OUT_MAX, err, (uint32_t)sizeof err);
    if (len < 0) {
        fprintf(stderr, "%s\n", err[0] ? err : "line 0: assembly failed");
        return 1;
    }

    f = fopen(argv[2], "wb");
    if (!f) {
        fprintf(stderr, "asm: cannot write '%s'\n", argv[2]);
        return 1;
    }
    if (len > 0 && fwrite(g_out, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "asm: write error on '%s'\n", argv[2]);
        fclose(f);
        return 1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "asm: write error on '%s'\n", argv[2]);
        return 1;
    }
    return 0;
}
