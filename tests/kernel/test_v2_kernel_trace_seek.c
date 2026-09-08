/* WS1-09, WS1-10, WS1-16: trace ring, snapshot seek/replay, determinism.
 * Drives the machine only through src/api/api.h; the counting program is tiny-C via tos_compile. */
#include "../testfw.h"
#include "api/api.h"
#include "kernel/snapshot.h"
#include <string.h>

static void push_str(const char *s) { while (*s) tos_con_push((uint8_t)*s++); }

static cpu_t read_cpu(void) { cpu_t c; memcpy(&c, tos_cpu_ptr(), sizeof c); return c; }

static int step_exact(uint32_t n) { return tos_step(n) == n && tos_stop_reason() == KSTOP_BUDGET; }

/* count.c-style program: prints 1..100, one per line, through a print_int with a global digit buffer. */
static const char COUNTER_SRC[] =
    "char digits[8];\n"
    "int print_int(int v) {\n"
    "    int i = 0;\n"
    "    if (v == 0) { putchar('0'); return 0; }\n"
    "    while (v > 0) {\n"
    "        digits[i] = '0' + v % 10;\n"
    "        v = v / 10;\n"
    "        i = i + 1;\n"
    "    }\n"
    "    while (i > 0) {\n"
    "        i = i - 1;\n"
    "        putchar(digits[i]);\n"
    "    }\n"
    "    return 0;\n"
    "}\n"
    "int main() {\n"
    "    int n = 1;\n"
    "    while (n <= 100) {\n"
    "        print_int(n);\n"
    "        putchar('\\n');\n"
    "        n = n + 1;\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

static uint8_t com[16384];
static uint32_t com_len;
static char err[256];

static int compile_counter(void) {
    int n = tos_compile(TOS_LANG_C, COUNTER_SRC, (uint32_t)(sizeof COUNTER_SRC - 1),
                        com, (uint32_t)sizeof com, err, (uint32_t)sizeof err);
    if (n <= 0) return -1;
    com_len = (uint32_t)n;
    return 0;
}

static int create_with(uint8_t trace, uint32_t snap_interval) {
    tos_config_t cfg;
    kernel_config_default(&cfg);
    cfg.trace = trace;
    cfg.snap_interval = snap_interval;
    return tos_create(&cfg);
}

static const uint8_t SPIN[] = { 0xC3, 0x00, 0x01 };            /* 0100: JMP 0100H */

/* ---- WS1-09 ----------------------------------------------------------- */
static int t_trace_fetch_events(void) {
    static const uint8_t prog[] = { 0x3E, 0x05, 0x76 };        /* MVI A,5 ; HLT */
    const trace_event_t *ring;
    uint32_t head0, head1, i;
    int found = 0, saw_hlt_state = 0;
    ASSERT(create_with(1, 0) == 0);
    head0 = tos_trace_head();
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(2) == 2u);
    ASSERT(tos_state() == KS_SHELL);                           /* the HLT returned to the shell */
    head1 = tos_trace_head();
    ASSERT(head1 > head0);
    ASSERT(head1 - head0 <= TRACE_CAP);
    ring = tos_trace_ptr();
    for (i = head0; i < head1; i++) {
        const trace_event_t *e = &ring[i % TRACE_CAP];
        if (e->kind == (int)TR_STATE && e->value == 5 && e->addr == ((KS_RUNNING << 8) | KS_SHELL)) saw_hlt_state = 1;
    }
    for (i = head0; i < head1; i++) {
        const trace_event_t *e = &ring[i % TRACE_CAP];
        if (e->kind == (int)TR_FETCH && e->addr == 0x0100 && e->value == 0x3E) {
            uint32_t j;
            for (j = i + 1; j < head1; j++) {
                const trace_event_t *f = &ring[j % TRACE_CAP];
                if (f->kind == (int)TR_FETCH) { found = (f->addr == 0x0102 && f->value == 0x76); break; }
            }
            break;
        }
    }
    ASSERT(found);
    ASSERT(saw_hlt_state);
    ASSERT(tos_trace_count() >= 2u);
    ASSERT(tos_trace_count() <= TRACE_CAP);
    return 0;
}

static int t_trace_never_exceeds_cap(void) {
    uint32_t head0, head1, head2;
    ASSERT(create_with(1, 0) == 0);
    head0 = tos_trace_head();
    ASSERT(tos_load_com(SPIN, (uint32_t)sizeof SPIN) == 0);
    ASSERT(step_exact(70000));                                 /* > TRACE_CAP fetch events alone */
    head1 = tos_trace_head();
    ASSERT(head1 - head0 >= 70000u);
    ASSERT(tos_trace_count() <= TRACE_CAP);
    ASSERT(tos_trace_count() == TRACE_CAP);
    ASSERT(step_exact(70000));
    head2 = tos_trace_head();
    ASSERT(head2 > head1);                                     /* head keeps counting pushes */
    ASSERT(tos_trace_count() <= TRACE_CAP);
    return 0;
}

static int t_trace_disabled_pushes_nothing(void) {
    uint32_t h;
    ASSERT(create_with(0, 0) == 0);
    h = tos_trace_head();
    ASSERT(tos_load_com(SPIN, (uint32_t)sizeof SPIN) == 0);
    ASSERT(step_exact(1000));
    ASSERT(tos_trace_head() == h);
    ASSERT(tos_trace_count() == 0u);
    tos_trace_enable(1);
    ASSERT(step_exact(1000));
    ASSERT(tos_trace_head() > h);
    tos_trace_enable(0);
    h = tos_trace_head();
    ASSERT(step_exact(1000));
    ASSERT(tos_trace_head() == h);
    return 0;
}

/* ---- WS1-10 ----------------------------------------------------------- */
static uint8_t tape_ref[65536];

static int t_seek_replays_to_identical_tape(void) {
    cpu_t cref, cnow;
    int slot, found = 0;
    /* reference: uninterrupted run to 3000, chunked exactly like the replayed run */
    ASSERT(create_with(0, 1000) == 0);
    ASSERT(compile_counter() == 0);
    ASSERT(tos_load_com(com, com_len) == 0);
    ASSERT(step_exact(1000));
    ASSERT(step_exact(1000));
    ASSERT(step_exact(500));
    ASSERT(step_exact(500));
    ASSERT(tos_steps() == 3000u);
    memcpy(tape_ref, tos_tape_ptr(0), 65536);
    cref = read_cpu();
    /* interrupted run: same chunks, then travel back to 2500 and forward again */
    ASSERT(create_with(0, 1000) == 0);
    ASSERT(tos_load_com(com, com_len) == 0);
    ASSERT(step_exact(1000));
    ASSERT(step_exact(1000));
    ASSERT(step_exact(500));
    ASSERT(step_exact(500));
    ASSERT(tos_steps() == 3000u);
    ASSERT(tos_snapshot_count() >= 3);
    for (slot = 0; slot < (int)SNAPSHOT_SLOTS; slot++) if (tos_snapshot_step(slot) == 2000u) found = 1;
    ASSERT(found);
    ASSERT(tos_seek(2500) == 0);
    ASSERT(tos_steps() == 2500u);
    ASSERT(tos_state() != KS_HALT);
    ASSERT(step_exact(500));
    ASSERT(tos_steps() == 3000u);
    ASSERT(memcmp(tape_ref, tos_tape_ptr(0), 65536) == 0);
    cnow = read_cpu();
    ASSERT(cnow.pc == cref.pc && cnow.sp == cref.sp && cnow.flags == cref.flags);
    ASSERT(cnow.a == cref.a && cnow.b == cref.b && cnow.c == cref.c && cnow.d == cref.d);
    ASSERT(cnow.e == cref.e && cnow.h == cref.h && cnow.l == cref.l);
    return 0;
}

static int t_seek_without_snapshots_fails(void) {
    ASSERT(create_with(0, 0) == 0);                             /* snap_interval 0 = never */
    ASSERT(compile_counter() == 0);
    ASSERT(tos_load_com(com, com_len) == 0);
    ASSERT(step_exact(1000));
    ASSERT(step_exact(1000));
    ASSERT(step_exact(1000));
    ASSERT(tos_snapshot_count() >= 1);                          /* the step-0 anchor (+ the load anchor), no interval snapshots */
    ASSERT(tos_snapshot_count() <= 2);
    ASSERT(tos_snapshot_step(0) == 0u);
    ASSERT(tos_seek(2500) == 0);                                /* replayed from the anchor */
    ASSERT(tos_steps() == 2500u);
    ASSERT(tos_state() == KS_RUNNING);
    return 0;
}

/* ---- WS1-16 ----------------------------------------------------------- */
static uint8_t ref_tape[10][65536];
static cpu_t ref_cpu[10];
static uint32_t ref_cycles[10];
static char ref_out[65536];
static size_t ref_out_len;
static char cur_out[65536];
static size_t cur_out_len;

static void append_out(char *buf, size_t *len, size_t cap) {
    int c;
    while ((c = tos_con_pop()) >= 0) { if (*len + 1 < cap) buf[(*len)++] = (char)c; }
}

/* Runs to steps 1000..10000; feeds the shell `help` whenever it parks, so both runs see the same
 * bytes at the same steps. record=1 stores the checkpoints, record=0 compares against them. */
static int run_checkpoints(int record) {
    uint32_t k;
    int pushes = 0;
    char *ob = record ? ref_out : cur_out;
    size_t *ol = record ? &ref_out_len : &cur_out_len;
    *ol = 0;
    for (k = 1; k <= 10u; k++) {
        uint32_t target = k * 1000u, guard = 0;
        while (tos_steps() < target) {
            int r;
            (void)tos_step(target - tos_steps());
            r = tos_stop_reason();
            append_out(ob, ol, sizeof ref_out);
            if (r == KSTOP_HALT) return -1;
            if (r == KSTOP_WAIT_INPUT) {
                if (pushes >= 400) return -2;
                push_str("help\n");
                pushes++;
            }
            if (++guard > 100000u) return -3;
        }
        if (tos_steps() != target) return -4;
        if (record) {
            memcpy(ref_tape[k - 1], tos_tape_ptr(0), 65536);
            ref_cpu[k - 1] = read_cpu();
            ref_cycles[k - 1] = tos_cycles_lo();
        } else {
            cpu_t c = read_cpu(), *r = &ref_cpu[k - 1];
            if (memcmp(ref_tape[k - 1], tos_tape_ptr(0), 65536) != 0) return -(100 + (int)k);
            if (c.a != r->a || c.b != r->b || c.c != r->c || c.d != r->d || c.e != r->e) return -(200 + (int)k);
            if (c.h != r->h || c.l != r->l || c.sp != r->sp || c.pc != r->pc || c.flags != r->flags) return -(300 + (int)k);
            if (c.halted != r->halted) return -(400 + (int)k);
            if (tos_cycles_lo() != ref_cycles[k - 1]) return -(500 + (int)k);
        }
    }
    append_out(ob, ol, sizeof ref_out);
    return 0;
}

static int t_two_runs_identical(void) {
    tos_config_t cfg;
    kernel_config_default(&cfg);
    cfg.seed = 3;
    ASSERT(tos_create(&cfg) == 0);
    ASSERT(compile_counter() == 0);
    ASSERT(tos_load_com(com, com_len) == 0);
    ASSERT(run_checkpoints(1) == 0);
    ASSERT(tos_create(&cfg) == 0);                              /* second machine, same config */
    ASSERT(tos_load_com(com, com_len) == 0);
    ASSERT(run_checkpoints(0) == 0);
    ASSERT(tos_steps() == 10000u);
    ASSERT(ref_out_len > 0);
    ASSERT(ref_out_len == cur_out_len);
    ASSERT(memcmp(ref_out, cur_out, ref_out_len) == 0);
    return 0;
}

int main(void) {
    TEST("WS1-09: trace shows TR_FETCH 0100/3E then TR_FETCH 0102/76 for MVI A,5; HLT", t_trace_fetch_events);
    TEST("WS1-09: trace_count never exceeds TRACE_CAP while head keeps counting", t_trace_never_exceeds_cap);
    TEST("WS1-09: a disabled trace pushes nothing (config and runtime toggle)", t_trace_disabled_pushes_nothing);
    TEST("WS1-10: tos_seek(2500) then +500 steps reproduces the uninterrupted tape 0 at 3000", t_seek_replays_to_identical_tape);
    TEST("WS1-10: with snap_interval 0 only the step-0 anchor exists and tos_seek replays from it", t_seek_without_snapshots_fails);
    TEST("WS1-16: two machines with the same config and input agree at steps 1000..10000", t_two_runs_identical);
    printf("PASS: test_v2_kernel_trace_seek\n");
    RUN_ALL_TESTS();
}
