/* Breakpoints, all five kinds, through the public API: they fire, they report which one fired,
 * resuming makes progress, and a syscall's own tape traffic is watched too.
 *
 * The suite used to pass with PC breakpoints disabled entirely; every assertion here is one that
 * fails if the corresponding check is removed from src/kernel/kernel.c. */
#include "../testfw.h"
#include "api/api.h"
#include "emu/cpu.h"
#include "tos.h"

/* 0100: JMP 0103 / 0103: MVI A,7 / STA 4000 / LDA 4000 / HLT */
static const uint8_t PROG[] = {
    0xC3, 0x03, 0x01,             /* 0100 JMP 0103 */
    0x3E, 0x07,                   /* 0103 MVI A,7  */
    0x32, 0x00, 0x40,             /* 0105 STA 4000 */
    0x3A, 0x00, 0x40,             /* 0108 LDA 4000 */
    0x76                          /* 010B HLT      */
};

/* MVI A,0x02 / MVI C,'Z' / OUT 1 (CONOUT) / HLT */
static const uint8_t SYSCALL_PROG[] = { 0x3E, 0x02, 0x0E, 0x5A, 0xD3, 0x01, 0x76 };

/* SETDMA 4000 / SETTRK 0 / SETSEC 1 / READ, then HLT: the BIOS writes the tape itself. */
static const uint8_t DMA_PROG[] = {
    0x11, 0x00, 0x40,             /* LXI D,4000    */
    0x3E, 0x0C, 0xD3, 0x01,       /* MVI A,0Ch OUT 1  (SETDMA) */
    0x0E, 0x00, 0x3E, 0x0A, 0xD3, 0x01,  /* MVI C,0 MVI A,0Ah OUT 1 (SETTRK) */
    0x0E, 0x01, 0x3E, 0x0B, 0xD3, 0x01,  /* MVI C,1 MVI A,0Bh OUT 1 (SETSEC) */
    0x3E, 0x0D, 0xD3, 0x01,       /* MVI A,0Dh OUT 1  (READ)   */
    0x76                          /* HLT */
};

static const cpu_t *cpu(void) { return (const cpu_t *)tos_cpu_ptr(); }

static int run_until_stop(uint32_t budget, int max_rounds) {
    int i;
    for (i = 0; i < max_rounds; ++i) {
        (void)tos_step(budget);
        if (tos_stop_reason() != (int)KSTOP_BUDGET) return tos_stop_reason();
    }
    return (int)KSTOP_BUDGET;
}

static int t_pc_breakpoint_fires(void) {
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(PROG, (uint32_t)sizeof PROG) == 0);
    ASSERT(tos_bp_add(KBP_PC, 0x0105u, 0x0105u) == 0);
    ASSERT(run_until_stop(100u, 20) == (int)KSTOP_BREAKPOINT);
    ASSERT(tos_bp_hit() == 0);
    ASSERT(cpu()->pc == 0x0105u);                 /* stops before the instruction, not after */
    ASSERT(cpu()->a == 0x07u);                    /* the MVI before it did run */
    return 0;
}

static int t_resume_makes_progress(void) {
    uint32_t at_stop;
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(PROG, (uint32_t)sizeof PROG) == 0);
    ASSERT(tos_bp_add(KBP_PC, 0x0105u, 0x0105u) == 0);
    ASSERT(run_until_stop(100u, 20) == (int)KSTOP_BREAKPOINT);
    at_stop = tos_steps();

    /* Stepping on from a PC breakpoint executes the instruction it stopped in front of instead of
       reporting the same address again. */
    ASSERT(tos_step(1u) == 1u);
    ASSERT(tos_steps() == at_stop + 1u);
    ASSERT(cpu()->pc != 0x0105u);

    /* With the breakpoint out of the way the program finishes and the shell parks for input.
       (The shell is loaded at 0x0100 too, so the same address is live in its code as well.) */
    tos_bp_clear();
    ASSERT(run_until_stop(100u, 40) == (int)KSTOP_WAIT_INPUT);
    ASSERT(tos_state() == (int)KS_IDLE);
    ASSERT(tos_steps() > at_stop + 1u);
    return 0;
}

static int t_write_and_read_watchpoints(void) {
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(PROG, (uint32_t)sizeof PROG) == 0);
    ASSERT(tos_bp_add(KBP_WRITE, 0x4000u, 0x4000u) == 0);
    ASSERT(run_until_stop(100u, 20) == (int)KSTOP_BREAKPOINT);
    ASSERT(tos_bp_hit() == 0);
    ASSERT(cpu()->pc == 0x0108u);                 /* the STA completed, then the stop */

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(PROG, (uint32_t)sizeof PROG) == 0);
    ASSERT(tos_bp_add(KBP_READ, 0x4000u, 0x4000u) == 0);
    ASSERT(run_until_stop(100u, 20) == (int)KSTOP_BREAKPOINT);
    ASSERT(cpu()->pc == 0x010Bu);                 /* fired on the LDA, not on the earlier STA */
    return 0;
}

static int t_syscall_and_state_breakpoints(void) {
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(SYSCALL_PROG, (uint32_t)sizeof SYSCALL_PROG) == 0);
    ASSERT(tos_bp_add(KBP_SYSCALL, TOS_BIOS_CONOUT, TOS_BIOS_CONOUT) == 0);
    ASSERT(run_until_stop(100u, 20) == (int)KSTOP_BREAKPOINT);
    ASSERT(tos_bp_hit() == 0);

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(SYSCALL_PROG, (uint32_t)sizeof SYSCALL_PROG) == 0);
    ASSERT(tos_bp_add(KBP_STATE, (uint16_t)KS_SHELL, (uint16_t)KS_SHELL) == 0);
    ASSERT(run_until_stop(100u, 40) == (int)KSTOP_BREAKPOINT);
    ASSERT(tos_bp_hit() == 0);
    return 0;
}

static int t_bios_dma_write_is_watched(void) {
    /* The BIOS moves a sector into the tape without executing an instruction; a write watchpoint
       over the DMA window has to see it. */
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(DMA_PROG, (uint32_t)sizeof DMA_PROG) == 0);
    ASSERT(tos_bp_add(KBP_WRITE, 0x4000u, 0x40FFu) == 0);
    ASSERT(run_until_stop(100u, 40) == (int)KSTOP_BREAKPOINT);
    ASSERT(tos_bp_hit() == 0);
    return 0;
}

static int t_table_limits(void) {
    int i;
    ASSERT(tos_create(NULL) == 0);
    for (i = 0; i < 16; ++i) ASSERT(tos_bp_add(KBP_PC, (uint16_t)(0x0200u + i), (uint16_t)(0x0200u + i)) == i);
    ASSERT(tos_bp_add(KBP_PC, 0x0300u, 0x0300u) == -1);      /* the 17th has nowhere to go */
    tos_bp_clear();
    ASSERT(tos_bp_add(KBP_PC, 0x0300u, 0x0300u) == 0);       /* cleared: slot 0 is free again */
    ASSERT(tos_bp_add(-1, 0, 0) == -1);                      /* not a kind */
    ASSERT(tos_bp_add(99, 0, 0) == -1);
    return 0;
}

static int t_no_breakpoint_no_stop(void) {
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(PROG, (uint32_t)sizeof PROG) == 0);
    ASSERT(run_until_stop(100u, 20) == (int)KSTOP_WAIT_INPUT);
    ASSERT(tos_bp_hit() == -1);
    return 0;
}

int main(void) {
    TEST("WS3-03: a PC breakpoint stops before its instruction and reports its index", t_pc_breakpoint_fires);
    TEST("WS3-03: resuming from a breakpoint makes progress instead of re-firing", t_resume_makes_progress);
    TEST("WS3-03: read and write watchpoints fire on the access that touches the range", t_write_and_read_watchpoints);
    TEST("WS3-03: syscall and state breakpoints fire", t_syscall_and_state_breakpoints);
    TEST("WS3-03: a write watchpoint sees the tape a BIOS call wrote itself", t_bios_dma_write_is_watched);
    TEST("WS3-03: the table holds 16, refuses the 17th, and clears", t_table_limits);
    TEST("WS3-03: with no breakpoint set the same program runs to HALT", t_no_breakpoint_no_stop);
    printf("PASS: test_v2_breakpoints\n");
    RUN_ALL_TESTS();
}
