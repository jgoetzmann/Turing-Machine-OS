/* WS5-01: tiny-C int arithmetic is 16-bit (SPEC S3 semantics + lexical rules).
 * Each program is compiled with cc_compile_buf, loaded with tos_load_com, stepped with a bounded
 * budget and its console bytes are checked. */
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

/* Fresh machine (cfg NULL = defaults), optional console input pushed first, program loaded into the
 * TPA, stepped until it hands control back to the shell (which parks on READLINE), halts, or the
 * budget is spent. Console bytes land in g_out. 0 ok / -1 when create or load failed. */
static int run_prog(const tos_config_t *cfg, const uint8_t *com, uint32_t len, const char *input)
{
    uint32_t total = 0;
    int guard = 100000;
    if (tos_create(cfg) != 0) return -1;
    while (tos_con_pop() >= 0) { /* discard output left over from a previous machine */ }
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

static int t_mul_300_100(void)
{
    int len = compile_c(PRINT_INT "int main() { print_int(300 * 100); " NL " return 0; }\n");
    ASSERT(len > 0);
    ASSERT(len <= (int)TOS_TPA_SIZE);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(g_steps > 0);
    ASSERT(out_is("30000\n"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

static int t_neg5_div_2(void)
{
    int len = compile_c(PRINT_INT "int main() { print_int(-5 / 2); " NL " return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("-2\n"));
    return 0;
}

static int t_65535_plus_1(void)
{
    int len = compile_c(PRINT_INT
        "int main() { int a; print_int(65535 + 1); " NL " a = 65535; print_int(a + 1); " NL " return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("0\n0\n"));
    return 0;
}

static int t_signed_compare(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int a;\n"
        "  print_int(-1 < 1); " NL "\n"
        "  print_int(-1 > 1); " NL "\n"
        "  a = 32767; print_int(a + 1 < 0); " NL "\n"
        "  print_int(32767 + 1 < 0); " NL "\n"
        "  a = 0x8000; print_int(a < 0); " NL "\n"
        "  print_int(-32768 < 32767); " NL "\n"
        "  print_int(a == -32768); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("1\n0\n1\n1\n1\n1\n1\n"));
    return 0;
}

static int t_char_stores_truncate(void)
{
    int len = compile_c(PRINT_INT
        "char g;\n"
        "int main() { char c = 300; print_int(c); " NL " c = 511; print_int(c); " NL
        " g = 300; print_int(g); " NL " return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("44\n255\n44\n"));
    return 0;
}

static int t_div_mod_toward_zero(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int z; z = 0;\n"
        "  print_int(7 / z); " NL "\n"
        "  print_int(-7 % 3); " NL "\n"
        "  print_int(7 % -3); " NL "\n"
        "  print_int(-7 / 2); " NL "\n"
        "  print_int(7 / -2); " NL "\n"
        "  print_int(100 / 7); " NL "\n"
        "  print_int(100 % 7); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("0\n-1\n1\n-3\n-3\n14\n2\n"));
    return 0;
}

static int t_shift_char_wrap(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int m; int a; char c; char d;\n"
        "  m = -1; print_int(m >> 15); " NL "\n"
        "  print_int(m >> 1); " NL "\n"
        "  c = 200; print_int(c + 100); " NL "\n"
        "  d = 255; print_int(d + 1); " NL "\n"
        "  d = d + 1; print_int(d); " NL "\n"
        "  a = 200 * 200; print_int(a); " NL "\n"
        "  print_int(200 * 200); " NL "\n"
        "  a = 1000; a = a * 70; print_int(a); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    /* 200*200 = 40000 -> -25536 in 16 bits; 70000 -> 4464 */
    ASSERT(out_is("1\n32767\n300\n256\n0\n-25536\n-25536\n4464\n"));
    return 0;
}

static int t_lexical(void)
{
    int len = compile_c(
        "// line comment before everything\n"
        "/* block\n   comment */\n"
        PRINT_INT
        "int main() { /* inline */ int x = 0x10; // trailing\n"
        "  print_int(0xFF); " NL "\n"
        "  print_int(0x1234); " NL "\n"
        "  print_int('\\n'); " NL "\n"
        "  print_int('\\\\'); " NL "\n"
        "  print_int('\\''); " NL "\n"
        "  print_int('\\0'); " NL "\n"
        "  print_int('A'); " NL "\n"
        "  print_int(x); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("255\n4660\n10\n92\n39\n0\n65\n16\n"));
    return 0;
}

/* failure: print_int is not an intrinsic, so a program that never defines it must not compile */
static int t_missing_print_int_is_error(void)
{
    int len = compile_c("int main() { print_int(300 * 100); return 0; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined function 'print_int'") != NULL);
    return 0;
}

int main(void)
{
    TEST("WS5-01: print_int(300*100) prints 30000", t_mul_300_100);
    TEST("WS5-01: -5 / 2 prints -2 (truncate toward zero)", t_neg5_div_2);
    TEST("WS5-01: 65535 + 1 prints 0 (constant and runtime)", t_65535_plus_1);
    TEST("WS5-01: <,<=,>,>= are signed 16-bit compares", t_signed_compare);
    TEST("WS5-01: char c = 300 stores 44 (store truncates)", t_char_stores_truncate);
    TEST("WS5-01: / and % truncate toward zero, /0 yields 0", t_div_mod_toward_zero);
    TEST("WS5-01: >> is logical, chars zero-extend, 16-bit wrap", t_shift_char_wrap);
    TEST("WS5-01: hex numbers, char escapes and comments (S3 lexical)", t_lexical);
    TEST("WS5-01: undefined print_int is 'undefined function' (failure)", t_missing_print_int_is_error);
    printf("PASS: test_v2_ws5_01_arith\n");
    RUN_ALL_TESTS();
}
