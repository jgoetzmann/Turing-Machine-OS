#include "../../src/compiler/compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASE_DIR "build/tests/compiler/cases_cc"

static void failf(const char *msg, const char *case_name) {
    fprintf(stderr, "FAIL: %s", msg);
    if (case_name != NULL) {
        fprintf(stderr, " [%s]", case_name);
    }
    fputc('\n', stderr);
    exit(1);
}

static void write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    size_t len = strlen(text);
    if (fp == NULL) {
        failf("unable to open source file", path);
    }
    if (fwrite(text, 1u, len, fp) != len) {
        fclose(fp);
        failf("unable to write source file", path);
    }
    fclose(fp);
}

static long file_size(const char *path) {
    FILE *fp = fopen(path, "rb");
    long size;
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    size = ftell(fp);
    fclose(fp);
    return size;
}

static void compile_case(const char *name, const char *src) {
    char src_path[256];
    char out_path[256];
    long out_size;

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

    out_size = file_size(out_path);
    if (out_size <= 0) {
        failf("compiled output is empty", name);
    }
}

int main(void) {
    if (system("mkdir -p " CASE_DIR) != 0) {
        failf("failed to create compiler case dir", CASE_DIR);
    }

    compile_case("add", "int main(){ return 3+4; }");
    compile_case("loop", "int main(){ int i; i=0; while(i<4){ i=i+1; } return i; }");
    compile_case("call", "int f(){ return 7; } int main(){ return f(); }");
    compile_case("io", "int main(){ puts(\"HELLO\"); return 0; }");
    compile_case("logic", "int main(){ return (1&&2) || 0; }");

    puts("PASS: test_cc");
    return 0;
}
