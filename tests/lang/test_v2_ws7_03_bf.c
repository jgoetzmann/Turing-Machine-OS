/* WS7-03: Brainfuck compiler (SPEC S6). Unmatched-bracket errors, plus compiled programs run
 * through the tos_* API: . and , via the console, loops, ignored characters, byte cells, cell
 * placement at 0x4000 on tape 0 (1 tape) or tape 1 (>= 2 tapes). */
#include "../testfw.h"
#include "api/api.h"
#include "lang/bf.h"
#include <stdint.h>
#include <string.h>

#define RUN_BUDGET 2000000u

static uint8_t  g_bin[32768];
static char     g_err[512];
static char     g_out[16384];
static char     g_src[4096];
static uint32_t g_out_len;
static uint32_t g_steps;

static int compile_bf(const char *src)
{
    g_err[0] = 0;
    return bf_compile(src, (uint32_t)strlen(src), g_bin, (uint32_t)sizeof g_bin, g_err, (uint32_t)sizeof g_err);
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

static tos_config_t cfg_with(uint8_t tapes, uint32_t tape_len)
{
    tos_config_t c;
    kernel_config_default(&c);
    c.tapes = tapes;
    c.tape_len = tape_len;
    return c;
}

/* ---- success cases ---- */

static int t_plus_and_dot(void)
{
    int i, len;
    for (i = 0; i < 65; i++) g_src[i] = '+';
    g_src[65] = '.';
    g_src[66] = 0;
    len = compile_bf(g_src);
    ASSERT(len > 0);
    ASSERT(len <= (int)TOS_TPA_SIZE);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(g_steps > 0);
    ASSERT(out_is("A"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);          /* ends with HLT: back to the shell, not halted */
    ASSERT(tos_state() != KS_HALT);
    return 0;
}

static int t_comma_reads_console(void)
{
    int len = compile_bf(",+.,.");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, "aQ") == 0);
    ASSERT(out_is("bQ"));
    return 0;
}

static int t_loops(void)
{
    int len = compile_bf("++++++++[>++++++++<-]>+.+.+.");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("ABC"));
    /* nested loops: 5 * 13 = 65 */
    len = compile_bf("+++++[>+++++++++++++[>+<-]<-]>>.");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("A"));
    /* [-] clears a cell: 10 pluses, cleared, then 66 -> 'B' */
    len = compile_bf("++++++++++[-]++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++.");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("B"));
    return 0;
}

static int t_other_chars_ignored(void)
{
    size_t pos = 0;
    int i, len;
    strcpy(g_src, "Hello World\n");
    pos = strlen(g_src);
    for (i = 0; i < 66; i++) { g_src[pos++] = '+'; g_src[pos++] = 'x'; g_src[pos++] = ' '; }
    strcpy(g_src + pos, "\n  print it: . done\n");
    len = compile_bf(g_src);
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("B"));
    return 0;
}

static int t_cells_are_bytes(void)
{
    int len = compile_bf("-.+.>+++<<+++++.");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(g_out_len >= 3);
    ASSERT((unsigned char)g_out[0] == 0xFF);       /* 0 - 1 wraps to 255 */
    ASSERT((unsigned char)g_out[1] == 0x00);       /* 255 + 1 wraps to 0 */
    ASSERT((unsigned char)g_out[2] == 0x05);       /* cell left of cell 0 */
    ASSERT(tos_tape_ptr(0)[0x4001] == 3);          /* cell 1 on tape 0 for a 1-tape machine */
    ASSERT(tos_tape_ptr(0)[0x4000] == 0);
    return 0;
}

static int t_cells_on_tape_1_when_two_tapes(void)
{
    tos_config_t c = cfg_with(2, 65536);
    int len = compile_bf("+++>++++++.");
    ASSERT(len > 0);
    ASSERT(run_prog(&c, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(g_out_len >= 1);
    ASSERT((unsigned char)g_out[0] == 0x06);
    ASSERT(tos_tape_ptr(1)[0x4000] == 3);
    ASSERT(tos_tape_ptr(1)[0x4001] == 6);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0);
    ASSERT(tos_tape_ptr(0)[0x4001] == 0);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(tos_tape_ptr(0)[0x4000] == 3);
    ASSERT(tos_tape_ptr(0)[0x4001] == 6);
    return 0;
}

/* ---- failure cases ---- */

static int t_unmatched_open(void)
{
    ASSERT(compile_bf("[") == -1);
    ASSERT(strcmp(g_err, "line 1: unmatched '['") == 0);
    return 0;
}

static int t_unmatched_close(void)
{
    ASSERT(compile_bf("]") == -1);
    ASSERT(strcmp(g_err, "line 1: unmatched ']'") == 0);
    return 0;
}

static int t_unmatched_open_line_2(void)
{
    ASSERT(compile_bf("+\n[") == -1);
    ASSERT(strcmp(g_err, "line 2: unmatched '['") == 0);
    return 0;
}

static int t_unmatched_close_line_3(void)
{
    ASSERT(compile_bf("+\n+\n]") == -1);
    ASSERT(strcmp(g_err, "line 3: unmatched ']'") == 0);
    return 0;
}

static int t_nested_missing_close(void)
{
    ASSERT(compile_bf("[[]") == -1);
    ASSERT(strcmp(g_err, "line 1: unmatched '['") == 0);
    return 0;
}

static int t_nested_extra_close(void)
{
    ASSERT(compile_bf("[]]") == -1);
    ASSERT(strcmp(g_err, "line 1: unmatched ']'") == 0);
    return 0;
}

static int t_close_before_open(void)
{
    ASSERT(compile_bf("][") == -1);
    ASSERT(strcmp(g_err, "line 1: unmatched ']'") == 0);
    return 0;
}

static int t_comment_chars_do_not_close(void)
{
    ASSERT(compile_bf("a[b") == -1);
    ASSERT(strcmp(g_err, "line 1: unmatched '['") == 0);
    return 0;
}

int main(void)
{
    TEST("WS7-03: 65 '+' then '.' prints A and returns to the shell", t_plus_and_dot);
    TEST("WS7-03: ',' reads CONIN, '.' writes CONOUT", t_comma_reads_console);
    TEST("WS7-03: [ ] loops incl. nested and [-]", t_loops);
    TEST("WS7-03: non-command characters are ignored", t_other_chars_ignored);
    TEST("WS7-03: cells are bytes and live at 0x4000 on tape 0", t_cells_are_bytes);
    TEST("WS7-03: cells live on tape 1 when the machine has 2 tapes", t_cells_on_tape_1_when_two_tapes);
    TEST("WS7-03: '[' without ']' -> line 1: unmatched '[' (failure)", t_unmatched_open);
    TEST("WS7-03: ']' without '[' -> line 1: unmatched ']' (failure)", t_unmatched_close);
    TEST("WS7-03: unmatched '[' on line 2 (failure)", t_unmatched_open_line_2);
    TEST("WS7-03: unmatched ']' on line 3 (failure)", t_unmatched_close_line_3);
    TEST("WS7-03: [[] -> unmatched '[' (failure)", t_nested_missing_close);
    TEST("WS7-03: []] -> unmatched ']' (failure)", t_nested_extra_close);
    TEST("WS7-03: ][ -> unmatched ']' (failure)", t_close_before_open);
    TEST("WS7-03: ignored characters do not close a bracket (failure)", t_comment_chars_do_not_close);
    printf("PASS: test_v2_ws7_03_bf\n");
    RUN_ALL_TESTS();
}
