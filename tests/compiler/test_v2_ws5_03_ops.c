/* WS5-03: tiny-C operators and control flow (SPEC S3): & | ^ ~ << >>, compound assignment, ++/--
 * prefix and postfix, break, continue, do/while, else if, recursion, short-circuit && / ||. */
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

static int t_bitwise(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int m;\n"
        "  print_int(12 & 10); " NL "\n"
        "  print_int(12 | 10); " NL "\n"
        "  print_int(12 ^ 10); " NL "\n"
        "  print_int(~0); " NL "\n"
        "  print_int(~5); " NL "\n"
        "  print_int(1 << 10); " NL "\n"
        "  print_int(1024 >> 3); " NL "\n"
        "  m = 0x8000; print_int(m >> 15); " NL "\n"
        "  print_int(0xF0F0 & 0x0FF0); " NL "\n"
        "  print_int((5 & 4) | (2 ^ 3)); " NL "\n"
        "  m = 3; print_int(m << 4 >> 2); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(g_steps > 0);
    ASSERT(out_is("8\n14\n6\n-1\n-6\n1024\n128\n1\n240\n5\n12\n"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

static int t_compound_assign(void)
{
    int len = compile_c(PRINT_INT
        "int a[4];\n"
        "int main() {\n"
        "  int x; x = 10;\n"
        "  x += 5; print_int(x); " NL "\n"
        "  x -= 3; print_int(x); " NL "\n"
        "  x *= 2; print_int(x); " NL "\n"
        "  x /= 5; print_int(x); " NL "\n"
        "  x %= 3; print_int(x); " NL "\n"
        "  x |= 6; print_int(x); " NL "\n"
        "  x &= 5; print_int(x); " NL "\n"
        "  x ^= 3; print_int(x); " NL "\n"
        "  x <<= 2; print_int(x); " NL "\n"
        "  x >>= 1; print_int(x); " NL "\n"
        "  a[1] = 3; a[1] += 4; print_int(a[1]); " NL "\n"
        "  a[1] *= a[1]; print_int(a[1]); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("15\n12\n24\n4\n1\n7\n5\n6\n24\n12\n7\n49\n"));
    return 0;
}

static int t_inc_dec(void)
{
    int len = compile_c(PRINT_INT
        "int a[2];\n"
        "int main() {\n"
        "  int i; i = 5;\n"
        "  print_int(i++); " NL "\n"
        "  print_int(i); " NL "\n"
        "  print_int(++i); " NL "\n"
        "  print_int(i--); " NL "\n"
        "  print_int(--i); " NL "\n"
        "  a[0] = 1; a[0]++; ++a[0]; print_int(a[0]); " NL "\n"
        "  a[0]--; print_int(a[0]); " NL "\n"
        "  i = 3; i++; i++; --i; print_int(i); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("5\n6\n7\n7\n5\n3\n2\n4\n"));
    return 0;
}

static int t_break_continue(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int i; int s;\n"
        "  i = 0;\n"
        "  while (1) { i = i + 1; if (i == 5) break; }\n"
        "  print_int(i); " NL "\n"
        "  s = 0;\n"
        "  for (i = 0; i < 10; i++) { if (i % 2) continue; s += i; }\n"
        "  print_int(s); " NL "\n"
        "  s = 0; i = 0;\n"
        "  while (i < 10) { i++; if (i & 1) continue; s += i; }\n"
        "  print_int(s); " NL "\n"
        "  s = 0;\n"
        "  for (i = 0; i < 100; i++) { if (i == 7) break; s = s + 1; }\n"
        "  print_int(s); " NL "\n"
        "  i = 0; s = 0;\n"
        "  do { i++; if (i == 2) continue; s += i; } while (i < 4);\n"
        "  print_int(s); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("5\n20\n30\n7\n8\n"));
    return 0;
}

static int t_do_while(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int i; int c;\n"
        "  i = 0; do { i++; } while (i < 3); print_int(i); " NL "\n"
        "  c = 0; do { c++; } while (0); print_int(c); " NL "\n"
        "  i = 10; do i = i + 1; while (i < 5); print_int(i); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("3\n1\n11\n"));
    return 0;
}

static int t_else_if(void)
{
    int len = compile_c(
        "int classify(int x) {\n"
        "  if (x == 1) putchar('A');\n"
        "  else if (x == 2) putchar('B');\n"
        "  else if (x == 3) putchar('C');\n"
        "  else putchar('D');\n"
        "  return 0;\n"
        "}\n"
        "int main() {\n"
        "  classify(1); classify(2); classify(3); classify(9); " NL "\n"
        "  if (0) putchar('x'); else if (1) putchar('y'); else putchar('z'); " NL "\n"
        "  if (1) { if (0) putchar('p'); else putchar('q'); } else putchar('r'); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("ABCD\ny\nq\n"));
    return 0;
}

static int t_recursion(void)
{
    int len = compile_c(PRINT_INT
        "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\n"
        "int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }\n"
        "int noret(int n) { if (n > 0) return 5; }\n"
        "int main() {\n"
        "  print_int(fib(10)); " NL "\n"
        "  print_int(fact(7)); " NL "\n"
        "  print_int(fib(1)); " NL "\n"
        "  print_int(noret(0)); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("55\n5040\n1\n0\n"));
    return 0;
}

static int t_short_circuit_and_logic(void)
{
    int len = compile_c(PRINT_INT
        "int calls;\n"
        "int side() { calls = calls + 1; return 1; }\n"
        "int main() {\n"
        "  int r;\n"
        "  calls = 0;\n"
        "  r = 0 && side(); print_int(r); " NL " print_int(calls); " NL "\n"
        "  r = 1 || side(); print_int(r); " NL " print_int(calls); " NL "\n"
        "  r = 1 && side(); print_int(r); " NL " print_int(calls); " NL "\n"
        "  r = 0 || side(); print_int(r); " NL " print_int(calls); " NL "\n"
        "  r = 5 && 7; print_int(r); " NL "\n"
        "  r = 0 || 0; print_int(r); " NL "\n"
        "  print_int(!0); " NL " print_int(!5); " NL " print_int(!!7); " NL "\n"
        "  print_int(3 > 2); " NL "\n"
        "  print_int((5 == 5) + (1 != 1) + (2 <= 2) + (2 >= 3)); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("0\n0\n1\n0\n1\n1\n1\n2\n1\n0\n1\n0\n1\n1\n2\n"));
    return 0;
}

/* ---- failure cases (diagnostics named in S3) ---- */

static int t_compound_assign_undefined(void)
{
    int len = compile_c("int main() { q += 1; return 0; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined variable 'q'") != NULL);
    return 0;
}

static int t_postfix_undefined(void)
{
    int len = compile_c("int main() {\n  y++;\n  return 0;\n}\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:2:", 8) == 0);
    ASSERT(strstr(g_err, "undefined variable 'y'") != NULL);
    return 0;
}

static int t_recursive_call_to_undefined(void)
{
    int len = compile_c("int main() { int x; x = fib(3); return x; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined function 'fib'") != NULL);
    return 0;
}

int main(void)
{
    TEST("WS5-03: & | ^ ~ << >> print expected values", t_bitwise);
    TEST("WS5-03: compound assignments += -= *= /= %= |= &= ^= <<= >>=", t_compound_assign);
    TEST("WS5-03: ++/-- prefix and postfix on locals and array elements", t_inc_dec);
    TEST("WS5-03: break and continue in while/for/do loops", t_break_continue);
    TEST("WS5-03: do/while runs the body at least once", t_do_while);
    TEST("WS5-03: else if chains", t_else_if);
    TEST("WS5-03: recursion fib(10)==55, fact(7), missing return yields 0", t_recursion);
    TEST("WS5-03: && / || short-circuit, ! and comparisons yield 0/1", t_short_circuit_and_logic);
    TEST("WS5-03/WS5-08: compound assign to undefined name is an error (failure)", t_compound_assign_undefined);
    TEST("WS5-03/WS5-08: postfix ++ on undefined name is an error at its line (failure)", t_postfix_undefined);
    TEST("WS5-03/WS5-08: call to undefined function is an error (failure)", t_recursive_call_to_undefined);
    printf("PASS: test_v2_ws5_03_ops\n");
    RUN_ALL_TESTS();
}
