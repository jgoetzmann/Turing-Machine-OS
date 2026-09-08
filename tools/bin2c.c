#include <stdint.h>
#include <stdio.h>

/* bin2c <in> <out.c> <symbol>: emit `const uint8_t symbol[]` and `const uint32_t symbol_len`. */
int main(int argc, char **argv) {
    FILE *in;
    FILE *out;
    int ch;
    unsigned long n = 0;
    if (argc != 4) {
        fprintf(stderr, "usage: %s <in> <out.c> <symbol>\n", argv[0]);
        return 1;
    }
    in = fopen(argv[1], "rb");
    if (in == NULL) {
        perror(argv[1]);
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror(argv[2]);
        fclose(in);
        return 1;
    }
    fprintf(out, "#include <stdint.h>\nconst uint8_t %s[] = {\n", argv[3]);
    while ((ch = fgetc(in)) != EOF) {
        fprintf(out, "%s0x%02X,", (n % 16u == 0u) ? "    " : "", (unsigned)ch);
        n++;
        if (n % 16u == 0u) fputc('\n', out);
    }
    if (n == 0) fprintf(out, "    0x00");
    fprintf(out, "\n};\nconst uint32_t %s_len = %luu;\n", argv[3], n);
    fclose(in);
    if (fclose(out) != 0) {
        perror(argv[2]);
        return 1;
    }
    return 0;
}
