/* WS1-13: a generated tiny-C program larger than 8 KB whose code spans 0x20FC and which uses
 * && / || after that address prints the correct result. The filler is grown at test time until the
 * image is comfortably past 0x20FC (the spec fixes no code density), then the program is run through
 * the tos_* API and its console bytes are checked. */
#include "../testfw.h"
#include "api/api.h"
#include "compiler/compiler.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RUN_BUDGET 2000000u
#define MIN_IMAGE  9500      /* filler alone must reach past 0x20FC - 0x0100 = 8188 bytes */

static uint8_t  g_com[32768];
static char     g_err[512];
static char     g_out[16384];
static char     g_src[32768];
static uint32_t g_out_len;
static uint32_t g_steps;

static int compile_c(const char *src)
{
    g_err[0] = 0;
    return cc_compile_buf(src, (uint32_t)strlen(src), g_com, (uint32_t)sizeof g_com, g_err, (uint32_t)sizeof g_err);
}

static void drain_console(void)
{
    int ch;
    g_out_len = 0;
    while ((ch = tos_con_pop()) >= 0) {
        if (g_out_len < (uint32_t)sizeof g_out - 1u) g_out[g_out_len++] = (char)ch;
    }
    g_out[g_out_len] = 0;
}

static int run_prog(const tos_config_t *cfg, const uint8_t *com, uint32_t len)
{
    uint32_t total = 0;
    int guard = 100000;
    if (tos_create(cfg) != 0) return -1;
    while (tos_con_pop() >= 0) { }
    if (tos_load_com(com, len) != 0) return -1;
    while (total < RUN_BUDGET && guard-- > 0) {
        uint32_t chunk = RUN_BUDGET - total;
        uint32_t n;
        int r;
        if (chunk > 4096u) chunk = 4096u;
        n = tos_step(chunk);
        total += n;
        r = tos_stop_reason();
        if (r == KSTOP_HALT || r == KSTOP_WAIT_INPUT) break;
        if (n == 0 && r != KSTOP_VSYNC && r != KSTOP_BREAKPOINT) break;
    }
    g_steps = total;
    drain_console();
    return 0;
}

static int out_is(const char *expect)
{
    return strncmp(g_out, expect, strlen(expect)) == 0;
}

#define PRINT_INT \
    "int print_int(int n) {\n" \
    "  int m;\n" \
    "  m = n;\n" \
    "  if (m < 0) { putchar('-'); m = 0 - m; }\n" \
    "  if (m >= 10) print_int(m / 10);\n" \
    "  putchar('0' + m % 10);\n" \
    "  return 0;\n" \
    "}\n"

static size_t app(size_t pos, const char *s) { size_t n = strlen(s); memcpy(g_src + pos, s, n); return pos + n; }

/* Variant A: the filler lives in pad(); && / || follow it inside pad() and again in main(). */
static void gen_pad_variant(int n)
{
    size_t pos = 0;
    int i;
    char buf[1024];
    pos = app(pos, "int g;\n");
    pos = app(pos, PRINT_INT);
    pos = app(pos, "int pad() {\n");
    for (i = 0; i < n; i++) { pos = app(pos, "g = g + 1;"); if ((i % 16) == 15) pos = app(pos, "\n"); }
    snprintf(buf, sizeof buf,
        "\n  if (g == %d && g > 0) putchar('Y'); else putchar('N');\n"
        "  if (g == 0 || g == %d) putchar('Y'); else putchar('N');\n"
        "  if (g == 0 && g == %d) putchar('N'); else putchar('Y');\n"
        "  if (g == 1 || g == 2) putchar('N'); else putchar('Y');\n"
        "  return g;\n}\n", n, n, n);
    pos = app(pos, buf);
    snprintf(buf, sizeof buf,
        "int main() {\n"
        "  int r;\n"
        "  g = 0;\n"
        "  r = pad();\n"
        "  if (r == %d && g == r) putchar('Y'); else putchar('N');\n"
        "  if (r != %d || g != r) putchar('N'); else putchar('Y');\n"
        "  if (r > 100 && r < 30000 && g == r) putchar('Y'); else putchar('N');\n"
        "  if (r < 0 || r > 30000 || g != r) putchar('N'); else putchar('Y');\n"
        "  putchar('\\n');\n"
        "  print_int(r); putchar('\\n');\n"
        "  return 0;\n}\n", n, n);
    pos = app(pos, buf);
    g_src[pos] = 0;
}

/* Variant B: the filler and the && / || live in the same function (main), with loops after it. */
static void gen_main_variant(int n)
{
    size_t pos = 0;
    int i;
    char buf[1024];
    pos = app(pos, "int g;\n");
    pos = app(pos, PRINT_INT);
    pos = app(pos, "int main() {\n  int i; int c;\n  g = 0;\n");
    for (i = 0; i < n; i++) { pos = app(pos, "g = g + 1;"); if ((i % 16) == 15) pos = app(pos, "\n"); }
    snprintf(buf, sizeof buf,
        "\n  i = 0; c = 0;\n"
        "  while (i < 10 && (i %% 3 != 0 || i == 0)) { c = c + 1; i = i + 1; }\n"
        "  print_int(c); putchar('\\n');\n"
        "  print_int(i); putchar('\\n');\n"
        "  c = 0;\n"
        "  for (i = 0; i < 20; i = i + 1) { if (i < 5 || i >= 15) c = c + 1; }\n"
        "  print_int(c); putchar('\\n');\n"
        "  print_int(g == %d && (g > 1 || g < 0)); putchar('\\n');\n"
        "  print_int(g != %d || g == 0); putchar('\\n');\n"
        "  return 0;\n}\n", n, n);
    pos = app(pos, buf);
    g_src[pos] = 0;
}

/* grow the filler until the image passes MIN_IMAGE; returns statement count or -1 */
static int grow(void (*gen)(int), int *out_len)
{
    int n;
    for (n = 500; n <= 2700; n += 100) {
        int len;
        gen(n);
        len = compile_c(g_src);
        if (len < 0) return -1;
        if (len >= MIN_IMAGE) { *out_len = len; return n; }
    }
    return -1;
}

static int t_large_program_pad_variant(void)
{
    char expect[64];
    int len = 0;
    int n = grow(gen_pad_variant, &len);
    ASSERT(n > 0);
    ASSERT(len > 8192);
    ASSERT(len <= (int)TOS_TPA_SIZE);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len) == 0);
    ASSERT(g_steps > (uint32_t)n);
    snprintf(expect, sizeof expect, "YYYYYYYY\n%d\n", n);
    ASSERT(out_is(expect));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    ASSERT(tos_state() != KS_HALT);
    return 0;
}

static int t_large_program_main_variant(void)
{
    int len = 0;
    int n = grow(gen_main_variant, &len);
    ASSERT(n > 0);
    ASSERT(len > 8192);
    ASSERT(len <= (int)TOS_TPA_SIZE);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len) == 0);
    ASSERT(out_is("3\n3\n10\n1\n0\n"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

/* ---- failure cases ---- */

static int t_oversized_generated_program_rejected(void)
{
    int len;
    gen_pad_variant(2700);      /* >= 7 bytes per statement -> > 16128 bytes */
    ASSERT(strlen(g_src) < 32768u);
    len = compile_c(g_src);
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:", 6) == 0);
    ASSERT(strstr(g_err, "program too large") != NULL);
    return 0;
}

static int t_load_com_rejects_more_than_tpa(void)
{
    static uint8_t blob[TOS_TPA_SIZE + 1u];
    memset(blob, 0x00, sizeof blob);       /* NOPs */
    blob[0] = 0x76;                        /* HLT so a load that succeeds stays bounded */
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(blob, TOS_TPA_SIZE + 1u) == -1);
    ASSERT(tos_load_com(blob, TOS_TPA_SIZE) == 0);
    return 0;
}

int main(void)
{
    TEST("WS1-13: >8 KB program, &&/|| after 0x20FC in a later function prints the right result", t_large_program_pad_variant);
    TEST("WS1-13: >8 KB program, &&/|| after 0x20FC in the same function prints the right result", t_large_program_main_variant);
    TEST("WS1-13/WS5-08: generated program past 16128 bytes -> program too large (failure)", t_oversized_generated_program_rejected);
    TEST("WS1-13: tos_load_com refuses 16129 bytes and accepts 16128 (failure)", t_load_com_rejects_more_than_tpa);
    printf("PASS: test_v2_ws1_13_large\n");
    RUN_ALL_TESTS();
}
