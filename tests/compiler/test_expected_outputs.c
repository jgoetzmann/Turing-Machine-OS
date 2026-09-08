#include "../../src/bios/bios.h"
#include "../../src/compiler/compiler.h"
#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"
#include "../../src/hal/hal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASE_DIR "demos/hello"
#define BUILD_DIR "build/tests/compiler/expected_runtime"
#define MAX_BIN_SIZE 16384
#define MAX_OUTPUT 2048
#define MAX_STEPS 500000

typedef struct {
    const char *name;
    const char *src_rel;
    const char *expected_rel;
    const char *input;
} case_spec_t;

static const case_spec_t k_cases[] = {
    {"add", "add.c", "add.expected", NULL},
    {"strcat", "strcat.c", "strcat.expected", NULL},
    {"count", "count.c", "count.expected", NULL},
    {"echo", "echo.c", "echo.expected", "hello\n"},
    {"memtest", "memtest.c", "memtest.expected", NULL},
    {"hello", "hello.c", "hello.expected", NULL},
};

static const char *g_case = NULL;

static void failf(const char *msg, const char *detail) {
    fprintf(stderr, "FAIL: %s", msg);
    if (detail != NULL) fprintf(stderr, ": %s", detail);
    if (g_case != NULL) fprintf(stderr, " [case=%s]", g_case);
    fputc('\n', stderr);
    exit(1);
}

static int read_file(const char *path, char *buf, int cap) {
    FILE *fp = fopen(path, "rb");
    long sz;
    if (fp == NULL) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz < 0 || sz >= cap) {
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
    buf[sz] = '\0';
    return (int)sz;
}

static int load_binary(const char *path, unsigned char *buf, int cap) {
    FILE *fp = fopen(path, "rb");
    long sz;
    if (fp == NULL) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz <= 0 || sz > cap) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1u, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return (int)sz;
}

static void feed_stdin_input(const char *input) {
    if (input == NULL) return;
    while (*input != '\0') {
        if (hal_con_push((uint8_t)(unsigned char)*input) != 0) {
            failf("failed to stage console input", NULL);
        }
        input++;
    }
}

static void run_binary_capture(const unsigned char *bin, int bin_len, const char *input, char *out, int out_cap) {
    cpu_t cpu;
    int out_len = 0;
    int steps;
    mem_init(1u, 65536u);
    bios_init();
    cpu_init(&cpu);
    feed_stdin_input(input);
    for (int i = 0; i < bin_len; ++i) mem_write((uint16_t)(0x0100u + i), bin[i]);
    cpu.pc = 0x0100u;

    for (steps = 0; steps < MAX_STEPS && !cpu_halted(&cpu); ++steps) {
        cpu_step(&cpu);
        if (cpu.io_out_pending != 0u && cpu.io_out_port == 0x01u) {
            bios_dispatch(&cpu);
        }
        while (bios_pending_output()) {
            if (out_len + 1 >= out_cap) failf("captured output too large", NULL);
            out[out_len++] = bios_get_output();
        }
    }
    if (!cpu_halted(&cpu)) failf("program did not halt", NULL);
    out[out_len] = '\0';
}

static void run_case(const case_spec_t *spec) {
    char src_path[256];
    char expected_path[256];
    char out_path[256];
    char expected[MAX_OUTPUT];
    char actual[MAX_OUTPUT];
    unsigned char bin[MAX_BIN_SIZE];
    int bin_len;

    g_case = spec->name;
    if (snprintf(src_path, sizeof(src_path), CASE_DIR "/%s", spec->src_rel) >= (int)sizeof(src_path)) {
        failf("source path too long", NULL);
    }
    if (snprintf(expected_path, sizeof(expected_path), CASE_DIR "/%s", spec->expected_rel) >=
        (int)sizeof(expected_path)) {
        failf("expected path too long", NULL);
    }
    if (snprintf(out_path, sizeof(out_path), BUILD_DIR "/%s.com", spec->name) >= (int)sizeof(out_path)) {
        failf("output path too long", NULL);
    }

    if (read_file(expected_path, expected, (int)sizeof(expected)) < 0) failf("failed reading expected", expected_path);
    if (cc_compile(src_path, out_path) != 0) failf("cc_compile failed", src_path);
    bin_len = load_binary(out_path, bin, (int)sizeof(bin));
    if (bin_len <= 0) failf("failed loading compiled binary", out_path);
    run_binary_capture(bin, bin_len, spec->input, actual, (int)sizeof(actual));
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "FAIL: stdout mismatch [case=%s]\n", spec->name);
        fprintf(stderr, "  expected: [%s]\n", expected);
        fprintf(stderr, "  actual  : [%s]\n", actual);
        exit(1);
    }
}

int main(void) {
    const char *filter = NULL;
    int ran = 0;
    if (system("mkdir -p " BUILD_DIR) != 0) failf("failed to create build dir", BUILD_DIR);
    if (getenv("CC_CASE") != NULL && getenv("CC_CASE")[0] != '\0') {
        filter = getenv("CC_CASE");
    }
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i) {
        if (filter != NULL && strcmp(filter, k_cases[i].name) != 0) continue;
        run_case(&k_cases[i]);
        ran++;
    }
    /* A CC_CASE nobody recognises used to run zero cases and still print PASS, which turned every
       shell wrapper that passes one into a no-op the moment a case was renamed. */
    if (ran == 0) failf("no case ran", filter != NULL ? filter : "(no filter)");
    puts("PASS: test_expected_outputs");
    return 0;
}
