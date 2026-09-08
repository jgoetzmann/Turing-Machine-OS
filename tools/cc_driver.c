/* cc_driver <src.c> <out.com> — host CLI for the tiny-C v2 compiler (SPEC §S3, §S7).
 *
 * Exit 0 and write the flat .com image on success. On failure the diagnostic
 * "src.c:LINE:COL: message" (or an I/O error naming the file) is printed to stderr by
 * cc_compile() and the exit code is 1. */
#include "../src/compiler/compiler.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: cc_driver <src.c> <out.com>\n");
        return 1;
    }
    if (cc_compile(argv[1], argv[2]) != 0) {
        return 1; /* cc_compile already printed the diagnostic */
    }
    return 0;
}
