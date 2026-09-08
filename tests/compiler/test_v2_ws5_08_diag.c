/* WS5-08: cc_compile_buf diagnostics (SPEC S3). Every message named in the spec has a test:
 * expected ';', expected ')', undefined function, undefined variable, too many locals,
 * program too large, unexpected token. Text is "src.c:LINE:COL: message"; the tests check the
 * "src.c:LINE:" prefix and the message (COL is not pinned by the spec). All tests are failure cases. */
#include "../testfw.h"
#include "compiler/compiler.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_com[32768];
static char    g_err[512];
static char    g_src[32768];

static int compile_c(const char *src)
{
    g_err[0] = 0;
    return cc_compile_buf(src, (uint32_t)strlen(src), g_com, (uint32_t)sizeof g_com, g_err, (uint32_t)sizeof g_err);
}

static int t_missing_semicolon_spec_example(void)
{
    int len = compile_c("int main(){ int x = 1 }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "expected ';'") != NULL);
    return 0;
}

static int t_missing_semicolon_after_return(void)
{
    int len = compile_c("int main(){ return 0 }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "expected ';'") != NULL);
    return 0;
}

static int t_missing_semicolon_line_number(void)
{
    int len = compile_c(
        "int main() {\n"
        "  int x = 1;\n"
        "  int y = 2 return x;\n"
        "}\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:3:", 8) == 0);
    ASSERT(strstr(g_err, "expected ';'") != NULL);
    return 0;
}

static int t_undefined_function(void)
{
    int len = compile_c("int main(){ foo(); return 0; }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined function 'foo'") != NULL);
    return 0;
}

static int t_undefined_function_with_args(void)
{
    int len = compile_c("int main(){ return bar(1, 2); }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined function 'bar'") != NULL);
    return 0;
}

static int t_undefined_function_line_number(void)
{
    int len = compile_c("int main() {\n  return baz();\n}\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:2:", 8) == 0);
    ASSERT(strstr(g_err, "undefined function 'baz'") != NULL);
    return 0;
}

static int t_program_too_large_data(void)
{
    /* a 20000-byte initialised char array cannot fit a 16128-byte image */
    int len = compile_c("char big[20000] = \"x\";\nint main() { big[0] = 1; return 0; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:", 6) == 0);
    ASSERT(strstr(g_err, "program too large") != NULL);
    return 0;
}

static int t_program_too_large_code(void)
{
    /* 3500 straight-line statements: at least 7 bytes of code each -> > 16128 bytes */
    size_t pos = 0;
    int i, len;
    const char *head = "int x;\nint main() {\n";
    const char *tail = "return x;\n}\n";
    memcpy(g_src + pos, head, strlen(head)); pos += strlen(head);
    for (i = 0; i < 3500; i++) { memcpy(g_src + pos, "x=x+1;", 6); pos += 6; if ((i % 20) == 19) g_src[pos++] = '\n'; }
    memcpy(g_src + pos, tail, strlen(tail)); pos += strlen(tail);
    g_src[pos] = 0;
    ASSERT(pos < 32768u);
    len = compile_c(g_src);
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:", 6) == 0);
    ASSERT(strstr(g_err, "program too large") != NULL);
    return 0;
}

static int t_expected_rparen_expr(void)
{
    int len = compile_c("int main(){ int y = (1 + 2; return y; }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "expected ')'") != NULL);
    return 0;
}

static int t_expected_rparen_if(void)
{
    int len = compile_c("int main() {\n  if (1 { return 0; }\n  return 1;\n}\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:2:", 8) == 0);
    ASSERT(strstr(g_err, "expected ')'") != NULL);
    return 0;
}

static int t_expected_rparen_call(void)
{
    int len = compile_c("int main(){ putchar('a'; return 0; }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "expected ')'") != NULL);
    return 0;
}

static int t_undefined_variable(void)
{
    int len = compile_c("int main(){ return zz; }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined variable 'zz'") != NULL);
    return 0;
}

static int t_undefined_variable_assign_target(void)
{
    int len = compile_c("int main() {\n  int a;\n  a = 1;\n  zz = a;\n  return 0;\n}\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:4:", 8) == 0);
    ASSERT(strstr(g_err, "undefined variable 'zz'") != NULL);
    return 0;
}

static int t_too_many_locals(void)
{
    size_t pos = 0;
    int i, len;
    const char *head = "int main() {\n";
    const char *tail = "  return 0;\n}\n";
    memcpy(g_src + pos, head, strlen(head)); pos += strlen(head);
    for (i = 0; i < 40; i++) { pos += (size_t)snprintf(g_src + pos, sizeof g_src - pos, "  int v%d;\n", i); }
    memcpy(g_src + pos, tail, strlen(tail)); pos += strlen(tail);
    g_src[pos] = 0;
    len = compile_c(g_src);
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:", 6) == 0);
    ASSERT(strstr(g_err, "too many locals") != NULL);
    return 0;
}

static int t_unexpected_token_in_expression(void)
{
    int len = compile_c("int main(){ int x = ); return 0; }");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "unexpected token") != NULL);
    return 0;
}

static int t_unexpected_token_top_level(void)
{
    int len = compile_c("int main(){ return 0; }\n)\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:2:", 8) == 0);
    ASSERT(strstr(g_err, "unexpected token") != NULL);
    return 0;
}

int main(void)
{
    TEST("WS5-08: `int main(){ int x = 1 }` -> -1, src.c:1:, expected ';' (failure)", t_missing_semicolon_spec_example);
    TEST("WS5-08: `return 0 }` -> expected ';' (failure)", t_missing_semicolon_after_return);
    TEST("WS5-08: expected ';' reports the 1-based source line (failure)", t_missing_semicolon_line_number);
    TEST("WS5-08: unknown function -> undefined function 'foo' (failure)", t_undefined_function);
    TEST("WS5-08: unknown function with args -> undefined function 'bar' (failure)", t_undefined_function_with_args);
    TEST("WS5-08: undefined function reports line 2 (failure)", t_undefined_function_line_number);
    TEST("WS5-08: 20 KB data image -> program too large (failure)", t_program_too_large_data);
    TEST("WS5-08: >16128 bytes of code -> program too large (failure)", t_program_too_large_code);
    TEST("WS5-08: `(1 + 2;` -> expected ')' (failure)", t_expected_rparen_expr);
    TEST("WS5-08: `if (1 {` -> expected ')' at line 2 (failure)", t_expected_rparen_if);
    TEST("WS5-08: `putchar('a';` -> expected ')' (failure)", t_expected_rparen_call);
    TEST("WS5-08: `return zz;` -> undefined variable 'zz' (failure)", t_undefined_variable);
    TEST("WS5-08: assignment to undefined name reports its line (failure)", t_undefined_variable_assign_target);
    TEST("WS5-08: 40 locals -> too many locals (failure)", t_too_many_locals);
    TEST("WS5-08: `int x = );` -> unexpected token (failure)", t_unexpected_token_in_expression);
    TEST("WS5-08: stray `)` at top level -> unexpected token at line 2 (failure)", t_unexpected_token_top_level);
    printf("PASS: test_v2_ws5_08_diag\n");
    RUN_ALL_TESTS();
}
