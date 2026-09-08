/* WS7-02: Turing-machine language compiler (SPEC S5). Rejections named in S5 (bad rule, too many
 * tapes, duplicate rule) plus compiled programs run through the tos_* API: tape dump trimmed of
 * blanks, steps=N, k-tape rules, tape placement in the bank window. */
#include "../testfw.h"
#include "api/api.h"
#include "lang/tm.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RUN_BUDGET 2000000u

static uint8_t  g_bin[32768];
static char     g_err[512];
static char     g_out[16384];
static char     g_src[8192];
static char     g_expect[8192];
static uint32_t g_out_len;
static uint32_t g_steps;

static int compile_tm(const char *src)
{
    g_err[0] = 0;
    return tm_compile(src, (uint32_t)strlen(src), g_bin, (uint32_t)sizeof g_bin, g_err, (uint32_t)sizeof g_err);
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

/* compile + run on the default machine, then compare the console prefix */
static int tm_run_expect(const char *src, const char *expect)
{
    int len = compile_tm(src);
    if (len <= 0) { fprintf(stderr, "tm_compile failed: %s\n", g_err); return 0; }
    if (len > (int)TOS_TPA_SIZE) return 0;
    if (run_prog(NULL, g_bin, (uint32_t)len, NULL) != 0) return 0;
    if (!out_is(expect)) { fprintf(stderr, "expected '%s' got '%s'\n", expect, g_out); return 0; }
    return 1;
}

/* ---- success cases ---- */

static int t_two_rules_write_ones(void)
{
    ASSERT(tm_run_expect("q0 _ -> 1 R q1\nq1 _ -> 1 R halt\n", "11\nsteps=2\n"));
    ASSERT(g_steps > 0);
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    ASSERT(tos_state() != KS_HALT);
    return 0;
}

static int t_busy_beaver_2(void)
{
    ASSERT(tm_run_expect(
        "# 2-state busy beaver, blank is _\n"
        "q0 _ -> 1 R q1\n"
        "q0 1 -> 1 L q1\n"
        "q1 _ -> 1 L q0\n"
        "q1 1 -> 1 R halt\n", "1111\nsteps=6\n"));
    return 0;
}

static int t_input_and_no_matching_rule_halts(void)
{
    /* start is q0; the only rule is for q1, so the machine halts immediately with the input intact */
    ASSERT(tm_run_expect("start: q0\ninput: abc\nq1 a -> a R q1\n", "abc\nsteps=0\n"));
    return 0;
}

static int t_moves_left_and_stay(void)
{
    ASSERT(tm_run_expect(
        "input: ab\n"
        "q0 a -> a R q1\n"
        "q1 b -> b L q2\n"
        "q2 a -> c S halt\n", "cb\nsteps=3\n"));
    return 0;
}

static int t_trimming(void)
{
    /* interior blank kept, leading blank trimmed, all-blank tape prints an empty line */
    ASSERT(tm_run_expect("q0 _ -> 1 R q1\nq1 _ -> _ R q2\nq2 _ -> 1 R halt\n", "1_1\nsteps=3\n"));
    ASSERT(tm_run_expect("q0 _ -> _ R q1\nq1 _ -> 1 R halt\n", "1\nsteps=2\n"));
    ASSERT(tm_run_expect("q0 _ -> _ R halt\n", "\nsteps=1\n"));
    ASSERT(tm_run_expect("q0 _ -> 1 L q1\nq1 _ -> 1 L q2\nq2 _ -> _ L halt\n", "11\nsteps=3\n"));
    return 0;
}

static int t_custom_blank_and_default_start(void)
{
    /* blank is '0'; leading 0 trimmed; no start: line -> first rule's state (qa) */
    ASSERT(tm_run_expect("blank: 0\ninput: 1\nqa 1 -> 0 R qb\nqb 0 -> 1 R halt\n", "1\nsteps=2\n"));
    ASSERT(tm_run_expect("blank: .\nqz . -> x R halt\n", "x\nsteps=1\n"));
    return 0;
}

static int t_two_tape_rule_and_placement(void)
{
    tos_config_t c;
    int len = compile_tm("tapes: 2\nq0 (_,_) -> (a,b) (R,R) halt\n");
    ASSERT(len > 0);
    /* 1-tape machine: TM tape 1 lives on machine tape 0 at bank offset 8192, head at +4096 */
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("a\nb\nsteps=1\n"));
    ASSERT(tos_tape_ptr(0)[0x5000] == 'a');
    ASSERT(tos_tape_ptr(0)[0x7000] == 'b');
    /* 2-tape machine: TM tape 1 lives on machine tape 1 at bank offset 0 */
    c = cfg_with(2, 65536);
    ASSERT(run_prog(&c, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(out_is("a\nb\nsteps=1\n"));
    ASSERT(tos_tape_ptr(0)[0x5000] == 'a');
    ASSERT(tos_tape_ptr(1)[0x5000] == 'b');
    ASSERT(tos_tape_ptr(0)[0x7000] == 0);
    return 0;
}

static int t_comments_and_step_count(void)
{
    int i;
    size_t pos;
    ASSERT(tm_run_expect(
        "# a comment\n"
        "\n"
        "input: 111\n"
        "# another\n"
        "q0 1 -> 1 R q0\n"
        "q0 _ -> _ S halt\n", "111\nsteps=4\n"));
    /* a 300-cell input walks 301 rule applications: steps=N is not an 8-bit counter */
    pos = 0;
    memcpy(g_src + pos, "input: ", 7); pos += 7;
    for (i = 0; i < 300; i++) g_src[pos++] = '1';
    memcpy(g_src + pos, "\nq0 1 -> 1 R q0\nq0 _ -> _ S halt\n", 33); pos += 33;
    g_src[pos] = 0;
    for (i = 0; i < 300; i++) g_expect[i] = '1';
    strcpy(g_expect + 300, "\nsteps=301\n");
    ASSERT(tm_run_expect(g_src, g_expect));
    return 0;
}

/* ---- failure cases ---- */

static int t_bad_move_letter(void)
{
    ASSERT(compile_tm("q0 0 -> 1 X q1\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad rule") == 0);
    return 0;
}

static int t_bad_rule_line_number(void)
{
    ASSERT(compile_tm("# c\n# c\nq0 0 -> 1 X q1\n") == -1);
    ASSERT(strcmp(g_err, "line 3: bad rule") == 0);
    return 0;
}

static int t_bad_rule_missing_arrow(void)
{
    ASSERT(compile_tm("q0 0 1 R q1\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad rule") == 0);
    return 0;
}

static int t_bad_rule_missing_next_state(void)
{
    ASSERT(compile_tm("q0 0 -> 1 R\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad rule") == 0);
    return 0;
}

static int t_bad_rule_multichar_symbol(void)
{
    ASSERT(compile_tm("q0 ab -> 1 R q1\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad rule") == 0);
    return 0;
}

static int t_bad_rule_tuple_arity(void)
{
    ASSERT(compile_tm("tapes: 2\nq0 (0,1) -> (1) (R,R) q1\n") == -1);
    ASSERT(strcmp(g_err, "line 2: bad rule") == 0);
    return 0;
}

static int t_bad_rule_trailing_garbage(void)
{
    ASSERT(compile_tm("q0 0 -> 1 R q1 extra\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad rule") == 0);
    return 0;
}

static int t_bad_rule_double_move(void)
{
    ASSERT(compile_tm("q0 0 -> 1 RR q1\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad rule") == 0);
    return 0;
}

static int t_too_many_tapes(void)
{
    ASSERT(compile_tm("tapes: 5\nq0 (_,_,_,_,_) -> (1,1,1,1,1) (R,R,R,R,R) halt\n") == -1);
    ASSERT(strcmp(g_err, "line 1: too many tapes") == 0);
    return 0;
}

static int t_too_many_tapes_line_number(void)
{
    ASSERT(compile_tm("# c\ntapes: 6\nq0 _ -> 1 R halt\n") == -1);
    ASSERT(strcmp(g_err, "line 2: too many tapes") == 0);
    return 0;
}

static int t_duplicate_rule(void)
{
    ASSERT(compile_tm("q0 _ -> 1 R q1\nq0 _ -> 0 L q1\n") == -1);
    ASSERT(strncmp(g_err, "line ", 5) == 0);
    ASSERT(strstr(g_err, "duplicate rule") != NULL);
    return 0;
}

int main(void)
{
    TEST("WS7-02: two rules write 11, steps=2, program returns to the shell", t_two_rules_write_ones);
    TEST("WS7-02: 2-state busy beaver prints 1111 and steps=6", t_busy_beaver_2);
    TEST("WS7-02: input: directive; no matching rule halts with steps=0", t_input_and_no_matching_rule_halts);
    TEST("WS7-02: moves L and S", t_moves_left_and_stay);
    TEST("WS7-02: dump trims leading/trailing blanks, keeps interior blanks", t_trimming);
    TEST("WS7-02: blank: directive and default start state", t_custom_blank_and_default_start);
    TEST("WS7-02: tapes: 2 rule prints both tapes; placement on 1- and 2-tape machines", t_two_tape_rule_and_placement);
    TEST("WS7-02: comments ignored; steps=301 for a 300-cell walk", t_comments_and_step_count);
    TEST("WS7-02: `q0 0 -> 1 X q1` -> line 1: bad rule (failure)", t_bad_move_letter);
    TEST("WS7-02: bad rule reports line 3 (failure)", t_bad_rule_line_number);
    TEST("WS7-02: rule without -> is a bad rule (failure)", t_bad_rule_missing_arrow);
    TEST("WS7-02: rule without next state is a bad rule (failure)", t_bad_rule_missing_next_state);
    TEST("WS7-02: multi-char symbol is a bad rule (failure)", t_bad_rule_multichar_symbol);
    TEST("WS7-02: tuple arity mismatch is a bad rule at line 2 (failure)", t_bad_rule_tuple_arity);
    TEST("WS7-02: trailing garbage is a bad rule (failure)", t_bad_rule_trailing_garbage);
    TEST("WS7-02: move RR is a bad rule (failure)", t_bad_rule_double_move);
    TEST("WS7-02: tapes: 5 -> line 1: too many tapes (failure)", t_too_many_tapes);
    TEST("WS7-02: tapes: 6 on line 2 -> line 2: too many tapes (failure)", t_too_many_tapes_line_number);
    TEST("WS7-02: same (state, read) twice -> duplicate rule (failure)", t_duplicate_rule);
    printf("PASS: test_v2_ws7_02_tm\n");
    RUN_ALL_TESTS();
}
