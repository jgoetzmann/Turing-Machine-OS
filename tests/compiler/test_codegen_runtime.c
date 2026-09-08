#include "../../src/bios/bios.h"
#include "../../src/compiler/compiler.h"
#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASE_DIR "build/tests/compiler/cases"
#define MAX_BIN_SIZE 16384
#define MAX_OUTPUT 1024
#define MAX_STEPS 500000

static const char *g_case_name = NULL;

static void failf(const char *msg, const char *detail) {
    fprintf(stderr, "FAIL: %s", msg);
    if (detail != NULL) {
        fprintf(stderr, ": %s", detail);
    }
    if (g_case_name != NULL) {
        fprintf(stderr, " [case=%s]", g_case_name);
    }
    fputc('\n', stderr);
    exit(1);
}

static void write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    size_t len;
    if (fp == NULL) {
        failf("unable to open file for write", path);
    }
    len = strlen(text);
    if (fwrite(text, 1u, len, fp) != len) {
        fclose(fp);
        failf("unable to write source file", path);
    }
    fclose(fp);
}

static int load_binary(const char *path, unsigned char *buf, int cap) {
    FILE *fp = fopen(path, "rb");
    long sz;
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz < 0 || sz > cap) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (sz > 0 && fread(buf, 1u, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return (int)sz;
}

static uint8_t run_compiled_program(const unsigned char *bin, int bin_len, char *out, int out_cap) {
    cpu_t cpu;
    int steps;
    int out_len = 0;

    if (bin_len <= 0) {
        failf("compiled binary is empty", NULL);
    }

    mem_init(1u, 65536u);
    bios_init();
    cpu_init(&cpu);

    for (int i = 0; i < bin_len; ++i) {
        mem_write((uint16_t)(0x0100u + i), bin[i]);
    }
    cpu.pc = 0x0100u;

    for (steps = 0; steps < MAX_STEPS && !cpu_halted(&cpu); ++steps) {
        cpu_step(&cpu);
        if (cpu.io_out_pending != 0u && cpu.io_out_port == 0x01u) {
            bios_dispatch(&cpu);
        }
        while (bios_pending_output()) {
            if (out_len + 1 >= out_cap) {
                failf("captured output too large", NULL);
            }
            out[out_len++] = bios_get_output();
        }
    }

    if (!cpu_halted(&cpu)) {
        failf("program did not halt within step budget", NULL);
    }
    out[out_len] = '\0';
    return cpu.l;
}

static void run_case(const char *name, const char *src, const char *expected_out, int expected_a) {
    char src_path[256];
    char out_path[256];
    unsigned char bin[MAX_BIN_SIZE];
    char actual[MAX_OUTPUT];
    int bin_len;
    uint8_t ret_a;

    g_case_name = name;
    if (snprintf(src_path, sizeof(src_path), CASE_DIR "/%s.c", name) >= (int)sizeof(src_path)) {
        failf("source path too long", name);
    }
    if (snprintf(out_path, sizeof(out_path), CASE_DIR "/%s.com", name) >= (int)sizeof(out_path)) {
        failf("output path too long", name);
    }

    write_text_file(src_path, src);
    if (cc_compile(src_path, out_path) != 0) {
        failf("cc_compile failed", name);
    }
    bin_len = load_binary(out_path, bin, (int)sizeof(bin));
    if (bin_len <= 0) {
        failf("unable to load compiled binary", name);
    }
    ret_a = run_compiled_program(bin, bin_len, actual, (int)sizeof(actual));
    if (expected_out != NULL && strcmp(actual, expected_out) != 0) {
        fprintf(stderr, "FAIL: output mismatch for %s\n", name);
        fprintf(stderr, "  expected: [%s]\n", expected_out);
        fprintf(stderr, "  actual  : [%s]\n", actual);
        exit(1);
    }
    if (expected_a >= 0 && ret_a != (uint8_t)expected_a) {
        fprintf(stderr, "FAIL: return register mismatch for %s\n", name);
        fprintf(stderr, "  expected A: %d\n", expected_a);
        fprintf(stderr, "  actual A  : %u\n", (unsigned int)ret_a);
        exit(1);
    }
    g_case_name = NULL;
}

int main(void) {
    if (system("mkdir -p " CASE_DIR) != 0) {
        failf("failed to create case dir", CASE_DIR);
    }

    run_case("arith_add",
             "int main(){ return 3+4; }",
             NULL,
             7);
    run_case("while_count",
             "int main(){ int i; i=0; while(i<5){ i=i+1; } return i; }",
             NULL,
             5);
    run_case("mul_div",
             "int main(){ return (3*4)/2; }",
             NULL,
             6);
    run_case("func_call",
             "int val(){ return 90; } int main(){ return val(); }",
             NULL,
             90);
    run_case("if_else",
             "int main(){ if(2<1){ return 7; } else { return 9; } }",
             NULL,
             9);
    run_case("logical_ops",
             "int main(){ return (0||2) + (1&&0) + (1&&3); }",
             NULL,
             2);
    /* Regression: rhs of && used MOV B for ==, clobbering saved left; shell "help" broke. */
    run_case("nested_eq_and",
             "int main(){ if(7==7 && 8==8 && 9==9 && 10==10){ return 42; } return 0; }",
             NULL,
             42);
    run_case("nested_eq_or",
             "int main(){ if(1==2 || 0==0 || 0==1 || 0==0){ return 99; } return 0; }",
             NULL,
             99);
    run_case("linelen_empty",
             "int main(){ return linelen(); }",
             NULL,
             0);
    /* (true && false) || true — nested && under || uses scratch for both ops */
    run_case("mixed_and_or",
             "int main(){ if((1==1 && 0==1) || (1==1)){ return 7; } return 0; }",
             NULL,
             7);
    run_case("unary_ops",
             "int main(){ return !0 + !5 + -3; }",
             NULL,
             254);
    run_case("puts_literal",
             "int main(){ puts(\"HELLO\"); return 0; }",
             "HELLO\n",
             0);

    puts("PASS: test_codegen_runtime");
    return 0;
}
