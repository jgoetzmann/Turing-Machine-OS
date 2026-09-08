/* WS5-02: tiny-C global arrays (SPEC S3): int a[N] little-endian 2-byte elements, char s[N] = "txt"
 * zero-padded, puts(s), arbitrary index expressions, and the `local arrays are not supported` error. */
#include "../testfw.h"
#include "api/api.h"
#include "compiler/compiler.h"
#include <stdint.h>
#include <string.h>

#define RUN_BUDGET 1000000u

static uint8_t  g_com[32768];
static char     g_err[512];
static char     g_out[16384];
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

static int run_prog(const tos_config_t *cfg, const uint8_t *com, uint32_t len, const char *input)
{
    uint32_t total = 0;
    int guard = 100000;
    if (tos_create(cfg) != 0) return -1;
    while (tos_con_pop() >= 0) { }
    if (input) { const char *p = input; while (*p) tos_con_push((uint8_t)*p++); }
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
#define NL "putchar('\\n');"

static int t_int_array_sum(void)
{
    int len = compile_c(PRINT_INT
        "int a[4];\n"
        "int b[4] = {5, 6, 7, 8};\n"
        "int main() {\n"
        "  int i; int s;\n"
        "  a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4;\n"
        "  s = 0;\n"
        "  for (i = 0; i < 4; i = i + 1) s = s + a[i];\n"
        "  print_int(s); " NL "\n"
        "  s = 0; i = 0;\n"
        "  while (i < 4) { s = s + b[i]; i = i + 1; }\n"
        "  print_int(s); " NL "\n"
        "  a[3] = 1000; a[2] = 2000; print_int(a[2] + a[3]); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(g_steps > 0);
    ASSERT(out_is("10\n26\n3000\n"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

static int t_char_array_puts(void)
{
    int len = compile_c(
        "char s[8] = \"hi\";\n"
        "int main() { puts(s); s[0] = 'H'; puts(s); return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("hi\nHi\n"));
    return 0;
}

static int t_index_expressions(void)
{
    int len = compile_c(PRINT_INT
        "int a[8];\n"
        "char s[8] = \"abcdefg\";\n"
        "int two() { return 2; }\n"
        "int main() {\n"
        "  int i;\n"
        "  for (i = 0; i < 8; i = i + 1) a[i] = i * 10;\n"
        "  i = 2;\n"
        "  print_int(a[i * 2 - 1]); " NL "\n"          /* a[3] = 30 */
        "  print_int(a[two() + 1]); " NL "\n"          /* a[3] = 30 */
        "  print_int(a[a[0] + 5]); " NL "\n"           /* a[5] = 50 */
        "  a[i + 4] = a[1] + a[2]; print_int(a[6]); " NL "\n"   /* 30 */
        "  putchar(s[two() * 3]); putchar(s[7 - 7]); " NL "\n"  /* g a */
        "  s[i * 3] = 'Z'; puts(s); \n"                       /* abcdefZ */
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("30\n30\n50\n30\nga\nabcdefZ\n"));
    return 0;
}

static int t_int_elements_little_endian(void)
{
    const uint8_t *t0;
    int len = compile_c(PRINT_INT
        "__at(0x5000) int a[4];\n"
        "int main() { a[0] = 1; a[1] = 0x1234; a[3] = -1; print_int(a[1]); " NL " return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("4660\n"));
    t0 = tos_tape_ptr(0);
    ASSERT(t0[0x5000] == 0x01);
    ASSERT(t0[0x5001] == 0x00);
    ASSERT(t0[0x5002] == 0x34);
    ASSERT(t0[0x5003] == 0x12);
    ASSERT(t0[0x5006] == 0xFF);
    ASSERT(t0[0x5007] == 0xFF);
    return 0;
}

static int t_zero_pad_and_zero_globals(void)
{
    int len = compile_c(PRINT_INT
        "char s[8] = \"hi\";\n"
        "int g; char h; int z[3]; char t[4];\n"
        "int main() {\n"
        "  print_int(s[1]); " NL "\n"
        "  print_int(s[2]); " NL "\n"
        "  print_int(s[7]); " NL "\n"
        "  print_int(g + h + z[0] + z[2] + t[3]); " NL "\n"
        "  t[0] = 300; print_int(t[0]); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("105\n0\n0\n0\n44\n"));
    return 0;
}

static int t_puts_literal_and_array_copy(void)
{
    int len = compile_c(
        "char src[8] = \"hello\";\n"
        "char dst[8];\n"
        "int main() {\n"
        "  int i;\n"
        "  puts(\"abc\");\n"
        "  i = 0;\n"
        "  while (src[i]) { dst[i] = src[i]; i = i + 1; }\n"
        "  dst[i] = 0;\n"
        "  puts(dst);\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("abc\nhello\n"));
    return 0;
}

/* ---- failure cases ---- */

static int t_local_int_array_rejected(void)
{
    int len = compile_c("int main(){ int x[2]; }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "local arrays are not supported") != NULL);
    return 0;
}

static int t_local_char_array_rejected(void)
{
    int len = compile_c("int main() { char buf[16]; return 0; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "local arrays are not supported") != NULL);
    return 0;
}

static int t_local_array_after_scalars_rejected(void)
{
    int len = compile_c(
        "int main() {\n"
        "  int a;\n"
        "  int b;\n"
        "  int c[4];\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:4:", 8) == 0);
    ASSERT(strstr(g_err, "local arrays are not supported") != NULL);
    return 0;
}

static int t_undefined_array_name(void)
{
    int len = compile_c("int main() { nosuch[1] = 5; return nosuch[0]; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined variable 'nosuch'") != NULL);
    return 0;
}

int main(void)
{
    TEST("WS5-02: int a[4] sum loop and {..} initialiser", t_int_array_sum);
    TEST("WS5-02: char s[8] = \"hi\" puts hi, s[0]='H' puts Hi", t_char_array_puts);
    TEST("WS5-02: index expressions may be any int expression", t_index_expressions);
    TEST("WS5-02: int elements are 2 bytes little-endian", t_int_elements_little_endian);
    TEST("WS5-02: string initialiser zero-pads; globals are zero", t_zero_pad_and_zero_globals);
    TEST("WS5-02: puts(literal) and char array copy", t_puts_literal_and_array_copy);
    TEST("WS5-02: int x[2] local -> 'local arrays are not supported' (failure)", t_local_int_array_rejected);
    TEST("WS5-02: char buf[16] local -> 'local arrays are not supported' (failure)", t_local_char_array_rejected);
    TEST("WS5-02: local array after scalars rejected at its line (failure)", t_local_array_after_scalars_rejected);
    TEST("WS5-02/WS5-08: indexing an undefined name -> 'undefined variable' (failure)", t_undefined_array_name);
    printf("PASS: test_v2_ws5_02_arrays\n");
    RUN_ALL_TESTS();
}
