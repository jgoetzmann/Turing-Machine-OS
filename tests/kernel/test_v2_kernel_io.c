/* WS1-02a, WS1-02b, WS5-06, WS5-07: console parking, stepping after HALT, keys/CONST, VSYNC/TICKS.
 * Drives the machine only through src/api/api.h with hand-assembled 8080 programs. */
#include "../testfw.h"
#include "api/api.h"
#include <string.h>

#define MAX_STEPS 2000000u

static void push_str(const char *s) { while (*s) tos_con_push((uint8_t)*s++); }

static void drain_out(char *buf, size_t cap) {
    size_t n = 0;
    int c;
    while ((c = tos_con_pop()) >= 0) { if (n + 1 < cap) buf[n++] = (char)c; }
    buf[n] = 0;
}

/* Step until the kernel stops for something other than budget/vsync, or `budget` steps ran. */
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

static uint32_t meta_u32(unsigned off) {
    const uint8_t *m = tos_meta_ptr();
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) | ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}

/* MVI A,1 ; OUT 1 (CONIN) ; STA 4000H ; HLT */
static const uint8_t prog_getchar[] = { 0x3E, 0x01, 0xD3, 0x01, 0x32, 0x00, 0x40, 0x76 };

/* ---- WS1-02a ---------------------------------------------------------- */
static int t_getchar_parks(void) {
    uint32_t n;
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_load_com(prog_getchar, (uint32_t)sizeof prog_getchar) == 0);
    ASSERT(tos_state() == KS_RUNNING);
    n = tos_step(1000);
    ASSERT(n < 1000u);                       /* returned early */
    ASSERT(n == 2u);                         /* MVI, OUT; the syscall itself is not a step */
    ASSERT(tos_stop_reason() == KSTOP_WAIT_INPUT);
    ASSERT(tos_state() == KS_IDLE);
    ASSERT(tos_meta_ptr()[TOS_META_STATE] == KS_IDLE);
    ASSERT(tos_meta_ptr()[TOS_META_STOP] == KSTOP_WAIT_INPUT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
    ASSERT(tos_last_syscall() == (int)TOS_BIOS_CONIN);
    return 0;
}

static int t_getchar_still_no_input(void) {
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_load_com(prog_getchar, (uint32_t)sizeof prog_getchar) == 0);
    ASSERT(tos_step(1000) == 2u);
    ASSERT(tos_stop_reason() == KSTOP_WAIT_INPUT);
    /* Still nothing pushed: zero further steps, same stop reason, still parked. */
    ASSERT(tos_step(1000) == 0u);
    ASSERT(tos_stop_reason() == KSTOP_WAIT_INPUT);
    ASSERT(tos_state() == KS_IDLE);
    ASSERT(tos_step(1) == 0u);
    ASSERT(tos_stop_reason() == KSTOP_WAIT_INPUT);
    ASSERT(tos_steps() == 2u);
    return 0;
}

static int t_getchar_resumes_after_push(void) {
    cpu_t c;
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_load_com(prog_getchar, (uint32_t)sizeof prog_getchar) == 0);
    ASSERT(tos_step(1000) == 2u);
    ASSERT(tos_stop_reason() == KSTOP_WAIT_INPUT);
    tos_con_push((uint8_t)'x');
    (void)tos_step(1);                       /* IDLE -> SYSCALL -> CONIN completes -> RUNNING */
    ASSERT(tos_state() != KS_IDLE);
    ASSERT(tos_stop_reason() != KSTOP_WAIT_INPUT);
    c = read_cpu();
    ASSERT(c.a == (uint8_t)'x');
    (void)tos_step(1);                       /* STA 4000H has certainly run by now (at most STA, HLT) */
    ASSERT(tos_tape_ptr(0)[0x4000] == (uint8_t)'x');
    ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
    return 0;
}

/* ---- WS1-02b ---------------------------------------------------------- */
static int t_step_after_halt_command(void) {
    char out[512];
    uint32_t s;
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);   /* shell at its prompt */
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "A> ") != NULL);
    push_str("halt\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_HALT);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_COMMAND);
    s = tos_steps();
    ASSERT(tos_step(1000) == 0u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_step(1) == 0u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_steps() == s);
    ASSERT(tos_meta_ptr()[TOS_META_STATE] == KS_HALT);
    ASSERT(tos_meta_ptr()[TOS_META_STOP] == KSTOP_HALT);
    return 0;
}

static int t_step_after_tape_fault(void) {
    /* LDA 8000H ; HLT  on a 32K tape: the read faults, the kernel halts after the instruction */
    static const uint8_t prog[] = { 0x3A, 0x00, 0x80, 0x76 };
    cpu_t c;
    uint32_t s;
    ASSERT(fresh(32768u, 1) == 0);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(10) == 1u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_TAPE_FAULT);
    c = read_cpu();
    ASSERT(c.a == 0xFF);                     /* out-of-range reads return 0xFF */
    ASSERT(c.pc == 0x0103);                  /* the faulting instruction was finished */
    s = tos_steps();
    ASSERT(tos_step(1000) == 0u);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(tos_steps() == s);
    ASSERT(tos_state() == KS_HALT);
    return 0;
}

/* ---- WS5-06 ----------------------------------------------------------- */
/* IN 3 ; STA 4000H ; HLT */
static const uint8_t prog_keys[] = { 0xDB, 0x03, 0x32, 0x00, 0x40, 0x76 };
/* MVI A,5 (CONST) ; OUT 1 ; STA 4000H ; HLT */
static const uint8_t prog_const[] = { 0x3E, 0x05, 0xD3, 0x01, 0x32, 0x00, 0x40, 0x76 };

static int t_keys_w_space(void) {
    ASSERT(fresh(65536u, 1) == 0);
    tos_keys_set((uint8_t)(TOS_KEY_W | TOS_KEY_SPACE));
    ASSERT(tos_load_com(prog_keys, (uint32_t)sizeof prog_keys) == 0);
    ASSERT(tos_step(2) == 2u);               /* IN, STA — before the HLT and the shell */
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0x11);
    return 0;
}

static int t_keys_none(void) {
    ASSERT(fresh(65536u, 1) == 0);
    tos_tape_ptr(0)[0x4000] = 0x5A;          /* sentinel so a skipped STA is visible */
    ASSERT(tos_load_com(prog_keys, (uint32_t)sizeof prog_keys) == 0);
    ASSERT(tos_step(2) == 2u);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0x00);
    /* set then cleared: reads back as 0 */
    ASSERT(fresh(65536u, 1) == 0);
    tos_keys_set((uint8_t)(TOS_KEY_UP | TOS_KEY_ESC));
    tos_keys_set(0);
    tos_tape_ptr(0)[0x4000] = 0x5A;
    ASSERT(tos_load_com(prog_keys, (uint32_t)sizeof prog_keys) == 0);
    ASSERT(tos_step(2) == 2u);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0x00);
    return 0;
}

static int t_const_zero_before_push(void) {
    ASSERT(fresh(65536u, 1) == 0);
    tos_tape_ptr(0)[0x4000] = 0x5A;
    ASSERT(tos_load_com(prog_const, (uint32_t)sizeof prog_const) == 0);
    ASSERT(tos_step(3) == 3u);               /* MVI, OUT (CONST does not park), STA */
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_state() == KS_RUNNING);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0x00);
    ASSERT(tos_last_syscall() == (int)TOS_BIOS_CONST);
    return 0;
}

static int t_const_ff_after_push(void) {
    ASSERT(fresh(65536u, 1) == 0);
    tos_con_push((uint8_t)'x');
    ASSERT(tos_load_com(prog_const, (uint32_t)sizeof prog_const) == 0);
    ASSERT(tos_step(3) == 3u);
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0xFF);
    return 0;
}

/* ---- WS5-07 ----------------------------------------------------------- */
static int t_vsync_60(void) {
    static const uint8_t prog[] = {
        0x06, 0x3C,             /* 0100 MVI B,60          */
        0x3E, 0x06,             /* 0102 MVI A,6 (VSYNC)   */
        0xD3, 0x01,             /* 0104 OUT 1             */
        0x05,                   /* 0106 DCR B             */
        0xC2, 0x02, 0x01,       /* 0107 JNZ 0102H         */
        0x3E, 0x08,             /* 010A MVI A,8 (TICKS)   */
        0xD3, 0x01,             /* 010C OUT 1             */
        0x32, 0x00, 0x40,       /* 010E STA 4000H         */
        0x76                    /* 0111 HLT               */
    };
    uint32_t i;
    ASSERT(fresh(65536u, 1) == 0);
    ASSERT(tos_frame() == 0u);
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    for (i = 0; i < 60u; i++) {
        uint32_t n = tos_step(100000);
        ASSERT(n < 100000u);                 /* every VSYNC returns to the host at once */
        ASSERT(tos_stop_reason() == KSTOP_VSYNC);
        ASSERT(tos_frame() == i + 1u);
        ASSERT(tos_state() == KS_RUNNING);
        ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
        ASSERT(tos_last_syscall() == (int)TOS_BIOS_VSYNC);
    }
    ASSERT(tos_frame() == 60u);
    ASSERT(tos_meta_ptr()[TOS_META_STOP] == KSTOP_VSYNC);
    ASSERT(tos_step(5) == 5u);               /* DCR, JNZ (falls through), MVI, OUT (TICKS), STA */
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_tape_ptr(0)[0x4000] == 60);
    ASSERT(tos_frame() == 60u);
    ASSERT(meta_u32(TOS_META_FRAME) == 60u);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);   /* HLT -> shell -> prompt */
    ASSERT(tos_frame() == 60u);
    return 0;
}

static int t_no_vsync_no_frames(void) {
    /* MVI A,8 (TICKS) ; OUT 1 ; STA 4000H ; HLT — no frame was ever requested */
    static const uint8_t prog[] = { 0x3E, 0x08, 0xD3, 0x01, 0x32, 0x00, 0x40, 0x76 };
    ASSERT(fresh(65536u, 1) == 0);
    tos_tape_ptr(0)[0x4000] = 0x5A;
    ASSERT(tos_load_com(prog, (uint32_t)sizeof prog) == 0);
    ASSERT(tos_step(3) == 3u);
    ASSERT(tos_stop_reason() != KSTOP_VSYNC);
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_tape_ptr(0)[0x4000] == 0);
    ASSERT(tos_frame() == 0u);
    ASSERT(meta_u32(TOS_META_FRAME) == 0u);
    return 0;
}

int main(void) {
    TEST("WS1-02a: getchar with no input stops early with KSTOP_WAIT_INPUT in KS_IDLE", t_getchar_parks);
    TEST("WS1-02a: stepping again with still no input runs 0 steps and stays parked", t_getchar_still_no_input);
    TEST("WS1-02a: after tos_con_push('x') the syscall completes and A == 'x'", t_getchar_resumes_after_push);
    TEST("WS1-02b: tos_step after the halt command returns 0 steps with KSTOP_HALT", t_step_after_halt_command);
    TEST("WS1-02b: tos_step after a tape-fault HALT returns 0 steps with KSTOP_HALT", t_step_after_tape_fault);
    TEST("WS5-06: IN 3 after tos_keys_set(W|SPACE) stores 0x11", t_keys_w_space);
    TEST("WS5-06: IN 3 with no keys (or keys cleared) stores 0", t_keys_none);
    TEST("WS5-06: CONST returns 0 before any console byte is pushed", t_const_zero_before_push);
    TEST("WS5-06: CONST returns 0xFF after tos_con_push", t_const_ff_after_push);
    TEST("WS5-07: 60 VSYNCs each stop with KSTOP_VSYNC; frame==60 and TICKS==60", t_vsync_60);
    TEST("WS5-07: without VSYNC the frame counter and TICKS stay 0", t_no_vsync_no_frames);
    printf("PASS: test_v2_kernel_io\n");
    RUN_ALL_TESTS();
}
