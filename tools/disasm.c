/* disasm — host disassembler (SPEC §S7, `make disasm FILE=<path.com>`).
 *
 *   disasm <file.com> [orgHex]
 *
 * Prints one line per instruction: `AAAA: BB BB BB  MNEMONIC` — a 4-digit uppercase hex address,
 * the instruction bytes (1..3, two hex digits each, space separated, padded to 8 columns), two
 * spaces, then the mnemonic exactly as disasm_one() formats it ("MVI A,05H", "JMP 0123H", ...).
 * The mnemonic therefore always starts at column 17 (`cut -c17-` yields re-assemblable source).
 * The origin defaults to 0100H (the TPA); orgHex is parsed as hexadecimal ("0x" prefix optional).
 * A trailing instruction cut off by the end of the file is emitted as `DB 0xxH` lines so the
 * listing always re-assembles to the same bytes. */
#include "emu/disasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGE 65536u

/* +3 so disasm_one always has >= 3 readable bytes even at the very end of the image. */
static uint8_t image[MAX_IMAGE + 3];

int main(int argc, char **argv)
{
    FILE         *f;
    size_t        n;
    unsigned long org = 0x0100;
    size_t        i;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: disasm <file.com> [orgHex]\n");
        return 2;
    }
    if (argc == 3) {
        char *end = NULL;
        org = strtoul(argv[2], &end, 16);
        if (end == argv[2] || *end != '\0' || org > 0xFFFFul) {
            fprintf(stderr, "disasm: bad origin '%s' (expected hex, e.g. 0100)\n", argv[2]);
            return 2;
        }
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "disasm: cannot open '%s'\n", argv[1]);
        return 1;
    }
    n = fread(image, 1, MAX_IMAGE, f);
    fclose(f);
    memset(image + n, 0, sizeof image - n);

    i = 0;
    while (i < n) {
        char     text[64];
        char     hex[16];
        int      len, j;
        unsigned addr = (unsigned)((org + i) & 0xFFFFu);

        len = disasm_one(image + i, (uint16_t)addr, text, (int)sizeof text);
        if (len < 1) len = 1;
        if (len > 3) len = 3;
        if (i + (size_t)len > n) {
            /* Not enough bytes left for the decoded instruction: dump the byte as data. */
            snprintf(text, sizeof text, "DB 0%02XH", (unsigned)image[i]);
            len = 1;
        }

        hex[0] = '\0';
        for (j = 0; j < len; j++) {
            char b[4];
            snprintf(b, sizeof b, "%02X", (unsigned)image[i + (size_t)j]);
            if (j) strcat(hex, " ");
            strcat(hex, b);
        }

        printf("%04X: %-8s  %s\n", addr, hex, text);
        i += (size_t)len;
    }
    return 0;
}
