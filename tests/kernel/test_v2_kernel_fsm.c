/* WS1-06, WS1-07: the frozen transition table and every halt reason the kernel can write.
 * Drives the machine only through src/api/api.h (plus hal_init for the EOF case). */
#include "../testfw.h"
#include "api/api.h"
#include "hal/hal.h"
#include <string.h>

#define MAX_STEPS 4000000u

static void push_str(const char *s) { while (*s) tos_con_push((uint8_t)*s++); }

static void drain_out(char *buf, size_t cap) {
    size_t n = 0;
    int c;
    while ((c = tos_con_pop()) >= 0) { if (n + 1 < cap) buf[n++] = (char)c; }
    buf[n] = 0;
}

static int run_bounded(uint32_t budget) {
    uint32_t total = 0, iters = 0;
    while (total < budget && iters < 1000000u) {
        uint32_t chunk = budget - total;
        int r;
        if (chunk > 4096u) chunk = 4096u;
        total += tos_step(chunk);
        iters++;
        r = tos_stop_reason();
        if (r != KSTOP_BUDGET && r != KSTOP_VSYNC) return r;
    }
    return tos_stop_reason();
}

static cpu_t read_cpu(void) { cpu_t c; memcpy(&c, tos_cpu_ptr(), sizeof c); return c; }

static int fresh(uint32_t tape_len, uint8_t tapes) {
    tos_config_t cfg;
    kernel_config_default(&cfg);
    cfg.tape_len = tape_len;
    cfg.tapes = tapes;
    return tos_create(&cfg);
}

/* kernel.h's frozen table, copied verbatim. */
static const struct { int from; int to; const char *why; } FROZEN[KERNEL_TRANSITION_COUNT] = {
    { KS_BOOT,    KS_SHELL,   "shell loaded" },
    { KS_SHELL,   KS_SYSCALL, "OUT 01" },
    { KS_SYSCALL, KS_SHELL,   "syscall done" },
    { KS_SYSCALL, KS_RUNNING, "program loaded / syscall done" },
    { KS_RUNNING, KS_SYSCALL, "OUT 01" },
    { KS_RUNNING, KS_SHELL,   "program HLT" },
    { KS_SYSCALL, KS_IDLE,    "waiting for input" },
    { KS_IDLE,    KS_SYSCALL, "input available" },
    { KS_IDLE,    KS_HALT,    "console EOF" },
    { KS_SHELL,   KS_HALT,    "halt command or tape fault" },
    { KS_RUNNING, KS_HALT,    "tape fault" },
    { KS_SYSCALL, KS_HALT,    "console EOF" }
};

/* ---- WS1-06 ----------------------------------------------------------- */
static int t_table_pairs(void) {
    int i;
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_transition_count() == 12);
    ASSERT(tos_transition_count() == KERNEL_TRANSITION_COUNT);
    for (i = 0; i < KERNEL_TRANSITION_COUNT; i++) {
        ASSERT(tos_transition_from(i) == FROZEN[i].from);
        ASSERT(tos_transition_to(i) == FROZEN[i].to);
    }
    return 0;
}

static int t_table_why(void) {
    int i;
    for (i = 0; i < KERNEL_TRANSITION_COUNT; i++) {
        ASSERT(tos_transition_why(i) != NULL);
        ASSERT(strcmp(tos_transition_why(i), FROZEN[i].why) == 0);
    }
    return 0;
}

static int t_boot_fires_only_zero(void) {
    int i;
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_state() == KS_SHELL);
    ASSERT(tos_steps() == 0u);
    ASSERT(tos_transition_fired(0) == 1u);
    for (i = 1; i < KERNEL_TRANSITION_COUNT; i++) ASSERT(tos_transition_fired(i) == 0u);
    return 0;
}

static int t_boot_halt_fires(void) {
    char out[1024];
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    push_str("halt\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_HALT);
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "HALT\n") != NULL);
    ASSERT(tos_transition_fired(0) >= 1u);
    ASSERT(tos_transition_fired(1) >= 1u);
    ASSERT(tos_transition_fired(2) >= 1u);
    ASSERT(tos_transition_fired(9) >= 1u);
    ASSERT(tos_transition_fired(3) == 0u);
    ASSERT(tos_transition_fired(4) == 0u);
    ASSERT(tos_transition_fired(5) == 0u);
    /* the shell parked for its command line and was woken by it */
    ASSERT(tos_transition_fired(6) >= 1u);
    ASSERT(tos_transition_fired(7) >= 1u);
    return 0;
}

static int t_run_fires_3_and_5(void) {
    static const uint8_t hlt_only[] = { 0x76 };
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_disk_put_file(0, "X.COM", hlt_only, 1) == 0);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    push_str("run X.COM\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);   /* program HLT -> shell prompt again */
    ASSERT(tos_transition_fired(3) >= 1u);
    ASSERT(tos_transition_fired(5) >= 1u);
    ASSERT(tos_transition_fired(4) == 0u);                 /* a bare HLT never issues OUT 01 */
    ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
    ASSERT(tos_state() == KS_IDLE);
    push_str("halt\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_COMMAND);
    return 0;
}

/* ---- WS1-07 ----------------------------------------------------------- */
static int t_reason_command(void) {
    char out[1024];
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    push_str("halt\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_HALT);
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "HALT\n") != NULL);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_COMMAND);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_meta_ptr()[TOS_META_HALT_REASON] == (int)TOS_HALT_COMMAND);
    ASSERT(tos_meta_ptr()[TOS_META_STATE] == KS_HALT);
    ASSERT(tos_transition_fired(9) >= 1u);
    ASSERT(tos_step(10) == 0u);
    return 0;
}

static int t_reason_tape_fault_asm(void) {
    /* LXI H,4000H ; loop: MVI M,1 ; INX H ; JMP loop  — walks writes upward until the tape ends */
    static const uint8_t prog[] = { 0x21, 0x00, 0x40, 0x36, 0x01, 0x23, 0xC3, 0x03, 0x01 };
    cpu_t c;
    unsigned i;
    ASSERT(fresh(32768u, 1) == 0);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(run_bounded(400000u) == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_TAPE_FAULT);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_meta_ptr()[TOS_META_HALT_REASON] == (int)TOS_HALT_TAPE_FAULT);
    ASSERT(tos_transition_fired(10) >= 1u);
    c = read_cpu();
    ASSERT(c.pc == 0x0105);                  /* halted after finishing the faulting MVI M,1 at 0103 */
    ASSERT(c.h == 0x80 && c.l == 0x00);
    ASSERT(tos_tape_ptr(0)[0x4000] == 1);
    ASSERT(tos_tape_ptr(0)[TOS_STACK_TOP(32768u)] == 1);
    for (i = 0; i < 256u; i++) ASSERT(tos_tape_ptr(0)[TOS_DISPLAY_BASE(32768u) + i] == 1);
    ASSERT(tos_tape_ptr(0)[0x8000] == 0);   /* the faulting write was dropped */
    return 0;
}

static int t_reason_tape_fault_tinyc(void) {
    /* fault.c-style walker: writes upward from 0x4000 until the machine faults (skips the stack). */
    static const char SRC[] =
        "int a;\n"
        "int main() {\n"
        "    a = 0x4000;\n"
        "    while (1) {\n"
        "        if (a == 0x7000) a = 0x7E00;\n"
        "        poke(a, 1);\n"
        "        a = a + 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    static uint8_t com[16384];
    char err[256];
    int n;
    ASSERT(fresh(32768u, 1) == 0);
    n = tos_compile(TOS_LANG_C, SRC, (uint32_t)(sizeof SRC - 1), com, (uint32_t)sizeof com, err, (uint32_t)sizeof err);
    ASSERT(n > 0);
    ASSERT(tos_load_com(com, (uint32_t)n) == 0);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_TAPE_FAULT);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_transition_fired(10) >= 1u);
    ASSERT(tos_tape_ptr(0)[0x4000] == 1);
    ASSERT(tos_tape_ptr(0)[0x6FFF] == 1);
    ASSERT(tos_tape_ptr(0)[0x7EFF] == 1);
    return 0;
}

static int t_reason_bad_tape(void) {
    static const uint8_t prog[] = { 0x3E, 0x03, 0xD3, 0x02, 0x76 };   /* MVI A,3 ; OUT 2 ; HLT */
    ASSERT(fresh(65536u, 2) == 0);
    ASSERT(tos_tape_count() == 2);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(10) == 2u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_BAD_TAPE);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_meta_ptr()[TOS_META_HALT_REASON] == (int)TOS_HALT_BAD_TAPE);
    ASSERT(tos_transition_fired(10) >= 1u);
    ASSERT(tos_step(10) == 0u);
    return 0;
}

static int t_bad_tape_boundary(void) {
    static const uint8_t prog[] = { 0x3E, 0x01, 0xD3, 0x02, 0x76 };   /* MVI A,1 ; OUT 2 ; HLT */
    /* tape 1 does not exist on a 1-tape machine */
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(10) == 2u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_BAD_TAPE);
    ASSERT(tos_tape_selected() == 0);
    /* ... and is a plain selection on a 2-tape machine */
    ASSERT(fresh(65536u, 2) == 0);
    ASSERT(tos_tape_selected() == 0);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(2) == 2u);
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_state() == KS_RUNNING);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
    ASSERT(tos_tape_selected() == 1);
    ASSERT(tos_meta_ptr()[TOS_META_TAPE_SEL] == 1);
    return 0;
}

static int t_fault_after_instruction(void) {
    /* MVI A,55H ; STA 0FE00H ; HLT on a 32K tape: the STA faults, is finished, then the kernel halts */
    static const uint8_t prog[] = { 0x3E, 0x55, 0x32, 0x00, 0xFE, 0x76 };
    cpu_t c;
    ASSERT(fresh(32768u, 1) == 0);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(10) == 2u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_TAPE_FAULT);
    ASSERT(tos_state() == KS_HALT);
    c = read_cpu();
    ASSERT(c.pc == 0x0105);
    ASSERT(c.a == 0x55);
    ASSERT(tos_tape_ptr(0)[0xFE00] == 0);    /* dropped, not stored */
    return 0;
}

static int t_reason_eof(void) {
    const char *path = "build/tests/v2_empty_stdin.txt";
    FILE *f = fopen(path, "wb");
    int r;
    if (f == NULL) { path = "v2_empty_stdin.tmp"; f = fopen(path, "wb"); }
    ASSERT(f != NULL);
    fclose(f);
    ASSERT(tos_hal_option("raw", "0") == 0);
    ASSERT(tos_hal_option("stdin_script", path) == 0);
    hal_init();
    ASSERT(fresh(65536u, 1) == 0);
    r = run_bounded(MAX_STEPS);
    hal_shutdown();
    ASSERT(r == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_EOF);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_meta_ptr()[TOS_META_HALT_REASON] == (int)TOS_HALT_EOF);
    ASSERT(tos_transition_fired(8) + tos_transition_fired(11) >= 1u);
    ASSERT(tos_step(10) == 0u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    return 0;
}

int main(void) {
    TEST("WS1-06: tos_transition_count()==12 and every (from,to) pair matches the frozen table", t_table_pairs);
    TEST("WS1-06: tos_transition_why matches the frozen table text", t_table_why);
    TEST("WS1-06: a fresh boot has fired only index 0 (BOOT->SHELL), never 1..11", t_boot_fires_only_zero);
    TEST("WS1-06: boot + halt fires 0,1,2,9 at least once and never 3,4,5", t_boot_halt_fires);
    TEST("WS1-06: shell `run` of a HLT program fires 3 and 5 but not 4", t_run_fires_3_and_5);
    TEST("WS1-07: halt command -> TOS_HALT_COMMAND (state HALT, transition 9)", t_reason_command);
    TEST("WS1-07: upward write walk on a 32K tape -> TOS_HALT_TAPE_FAULT (transition 10)", t_reason_tape_fault_asm);
    TEST("WS1-07: fault.c-style tiny-C walker on a 32K tape -> TOS_HALT_TAPE_FAULT", t_reason_tape_fault_tinyc);
    TEST("WS1-07: OUT 2 with A=3 on 2 tapes -> TOS_HALT_BAD_TAPE", t_reason_bad_tape);
    TEST("WS1-07: OUT 2 with A=1 is BAD_TAPE on 1 tape but selects tape 1 on 2 tapes", t_bad_tape_boundary);
    TEST("WS1-07: a faulting STA is finished (pc advanced, write dropped) before the HALT", t_fault_after_instruction);
    TEST("WS1-07: console EOF (empty stdin_script) -> TOS_HALT_EOF", t_reason_eof);
    printf("PASS: test_v2_kernel_fsm\n");
    RUN_ALL_TESTS();
}
