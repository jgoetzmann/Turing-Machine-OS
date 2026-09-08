/* TuringOS v2 — tools/tmc.c: `tmc <in.tm> <out.com>` (SPEC §S5, §S7).
 * Reads a Turing-machine description, calls tm_compile(), writes the .com image.
 * Exit 0 on success; on a compile error prints `line N: message` to stderr and exits 1.
 * Built by the Makefile as: cc tools/tmc.c src/lang/tm.c -I./src -o build/tmc */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "lang/tm.h"

#define TMC_SRC_MAX  65536u
#define TMC_OUT_MAX  16128u   /* TOS_TPA_SIZE: a .com must fit the TPA */

static char    g_src[TMC_SRC_MAX];
static uint8_t g_out[TMC_OUT_MAX];

int main(int argc, char **argv)
{
    FILE *f;
    size_t n;
    int len;
    char err[192];

    if (argc != 3) {
        fprintf(stderr, "usage: tmc <in.tm> <out.com>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "tmc: cannot open %s\n", argv[1]);
        return 1;
    }
    n = fread(g_src, 1, sizeof g_src, f);
    fclose(f);
    if (n == sizeof g_src) {
        fprintf(stderr, "tmc: %s is too large (limit %u bytes)\n", argv[1], (unsigned)(TMC_SRC_MAX - 1u));
        return 1;
    }

    err[0] = 0;
    len = tm_compile(g_src, (uint32_t)n, g_out, (uint32_t)sizeof g_out, err, (uint32_t)sizeof err);
    if (len < 0) {
        fprintf(stderr, "%s\n", err[0] ? err : "line 1: bad rule");
        return 1;
    }
    f = fopen(argv[2], "wb");
    if (f == NULL) {
        fprintf(stderr, "tmc: cannot write %s\n", argv[2]);
        return 1;
    }
    if (fwrite(g_out, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "tmc: short write to %s\n", argv[2]);
        fclose(f);
        return 1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "tmc: error closing %s\n", argv[2]);
        return 1;
    }
    return 0;
}
