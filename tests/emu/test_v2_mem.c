/* tests/emu/test_v2_mem.c
 * Spec-derived tests for the tape module (src/emu/mem.h).
 * Behaviors: WS1-03, WS4-01a, WS4-01b, WS4-02a, WS4-08.
 * Written from the v2 specification only; no implementation was consulted.
 */
#include "../testfw.h"
#include "emu/mem.h"
#include "tos.h"
#include <stdint.h>
#include <string.h>

static int popcount32(const uint8_t bits[32])
{
    int n = 0;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = bits[i];
        while (b) { n += b & 1; b = (uint8_t)(b >> 1); }
    }
    return n;
}

/* ------------------------------------------------------------------ WS1-03 */

static int t_ws1_03_write_age(void)
{
    mem_init(1, 65536);
    mem_set_step(7);
    mem_write(0x4321, 9);
    ASSERT(mem_write_age(0)[0x4321] == 7);
    ASSERT(mem_peek(0, 0x4321) == 9);
    ASSERT(mem_read(0x4321) == 9);
    return 0;
}

static int t_ws1_03_dirty_pages(void)
{
    uint8_t out[32];
    mem_init(1, 65536);
    mem_set_step(7);
    mem_write(0x4321, 9);
    memset(out, 0xFF, sizeof out);
    mem_dirty_pages(out, 7);
    /* exactly one page dirty, and it is page 0x43 (bit order inside the byte is not specified) */
    ASSERT(popcount32(out) == 1);
    ASSERT(out[0x43 >> 3] != 0);
    for (int i = 0; i < 32; ++i) {
        if (i != (0x43 >> 3)) ASSERT(out[i] == 0);
    }
    return 0;
}

static int t_ws1_03_read_age(void)
{
    mem_init(1, 65536);
    mem_set_step(7);
    mem_write(0x4321, 9);
    ASSERT(mem_read_age(0)[0x4321] == 0);     /* a write alone is not a read */
    mem_set_step(9);
    ASSERT(mem_read(0x4321) == 9);
    ASSERT(mem_read_age(0)[0x4321] == 9);
    ASSERT(mem_write_age(0)[0x4321] == 7);    /* the read did not disturb the write stamp */
    return 0;
}

static int t_ws1_03_untouched_cells(void)
{
    uint8_t out[32];
    mem_init(1, 65536);
    mem_set_step(7);
    mem_write(0x4321, 9);
    ASSERT(mem_write_age(0)[0x4320] == 0);
    ASSERT(mem_write_age(0)[0x4322] == 0);
    ASSERT(mem_read_age(0)[0x4321] == 0);
    memset(out, 0xFF, sizeof out);
    mem_dirty_pages(out, 8);                  /* since_step later than the only write: nothing */
    ASSERT(popcount32(out) == 0);
    return 0;
}

static int t_ws1_03_init_resets(void)
{
    mem_init(1, 65536);
    mem_set_step(7);
    mem_write(0x4321, 9);
    (void)mem_read(0x4321);
    ASSERT(mem_cells_written() == 1);
    mem_init(1, 65536);
    ASSERT(mem_write_age(0)[0x4321] == 0);
    ASSERT(mem_read_age(0)[0x4321] == 0);
    ASSERT(mem_peek(0, 0x4321) == 0);
    ASSERT(mem_cells_written() == 0);
    ASSERT(mem_tape_count() == 1);
    ASSERT(mem_tape_len() == 65536);
    return 0;
}

/* ----------------------------------------------------------------- WS4-01a */

static int t_ws4_01a_tape1_isolated(void)
{
    mem_init(2, 65536);
    ASSERT(mem_tape_count() == 2);
    ASSERT(mem_selected_tape() == 0);
    ASSERT(mem_select_tape(1) == 0);
    ASSERT(mem_selected_tape() == 1);
    ASSERT(mem_fault() == 0);
    mem_set_step(3);
    mem_write(0x5000, 0x5A);
    ASSERT(mem_peek(0, 0x5000) == 0);
    ASSERT(mem_peek(1, 0x5000) == 0x5A);
    ASSERT(mem_read(0x5000) == 0x5A);         /* selected-tape view */
    ASSERT(mem_tape_raw(1)[0x5000] == 0x5A);
    ASSERT(mem_raw()[0x5000] == 0);
    ASSERT(mem_write_age(1)[0x5000] == 3);
    ASSERT(mem_write_age(0)[0x5000] == 0);
    ASSERT(mem_select_tape(0) == 0);
    ASSERT(mem_read(0x5000) == 0);            /* tape 0 view of the same window address */
    return 0;
}

static int t_ws4_01a_common_lands_on_tape0(void)
{
    mem_init(2, 65536);
    ASSERT(mem_select_tape(1) == 0);
    mem_write(0x0200, 0x77);
    ASSERT(mem_peek(0, 0x0200) == 0x77);
    ASSERT(mem_raw()[0x0200] == 0x77);
    ASSERT(mem_read(0x0200) == 0x77);
    ASSERT(mem_select_tape(0) == 0);
    ASSERT(mem_read(0x0200) == 0x77);
    /* scratch and display above the window are common too */
    ASSERT(mem_select_tape(1) == 0);
    mem_write((addr_t)TOS_DISPLAY_BASE(65536), 0xAA);
    ASSERT(mem_peek(0, (addr_t)TOS_DISPLAY_BASE(65536)) == 0xAA);
    mem_write((addr_t)TOS_SCRATCH_BASE(65536), 0xBB);
    ASSERT(mem_peek(0, (addr_t)TOS_SCRATCH_BASE(65536)) == 0xBB);
    return 0;
}

static int t_ws4_01a_window_edges(void)
{
    /* 64K: window is 0x4000..0xDFFF; 0x3FFF and 0xE000 are common */
    mem_init(2, 65536);
    ASSERT(mem_select_tape(1) == 0);
    mem_write(0x3FFF, 0x01);
    mem_write(0x4000, 0x02);
    mem_write((addr_t)TOS_BANK_END(65536), 0x03);
    mem_write((addr_t)TOS_SCRATCH_BASE(65536), 0x04);
    ASSERT(mem_peek(0, 0x3FFF) == 0x01);
    ASSERT(mem_peek(1, 0x4000) == 0x02);
    ASSERT(mem_peek(0, 0x4000) == 0);
    ASSERT(mem_peek(1, (addr_t)TOS_BANK_END(65536)) == 0x03);
    ASSERT(mem_peek(0, (addr_t)TOS_BANK_END(65536)) == 0);
    ASSERT(mem_peek(0, (addr_t)TOS_SCRATCH_BASE(65536)) == 0x04);
    /* 32K: window is 0x4000..0x5FFF; 0x6000 is common */
    mem_init(2, 32768);
    ASSERT(mem_select_tape(1) == 0);
    mem_write(0x5FFF, 0x05);
    mem_write(0x6000, 0x06);
    ASSERT(mem_peek(1, 0x5FFF) == 0x05);
    ASSERT(mem_peek(0, 0x5FFF) == 0);
    ASSERT(mem_peek(0, 0x6000) == 0x06);
    ASSERT(mem_fault() == 0);
    return 0;
}

/* ----------------------------------------------------------------- WS4-01b */

static int t_ws4_01b_two_tapes_select_2(void)
{
    mem_init(2, 65536);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_select_tape(2) == -1);
    ASSERT(mem_fault() == TOS_HALT_BAD_TAPE);
    return 0;
}

static int t_ws4_01b_one_tape_select_1(void)
{
    mem_init(1, 65536);
    ASSERT(mem_select_tape(0) == 0);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_select_tape(1) == -1);
    ASSERT(mem_fault() == TOS_HALT_BAD_TAPE);
    return 0;
}

static int t_ws4_01b_four_tapes_select_4(void)
{
    mem_init(4, 65536);
    ASSERT(mem_select_tape(3) == 0);
    ASSERT(mem_selected_tape() == 3);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_select_tape(4) == -1);
    ASSERT(mem_fault() == TOS_HALT_BAD_TAPE);
    return 0;
}

static int t_ws4_01b_select_255(void)
{
    mem_init(2, 32768);
    ASSERT(mem_select_tape(255) == -1);
    ASSERT(mem_fault() == TOS_HALT_BAD_TAPE);
    return 0;
}

static int t_ws4_01b_clear_then_ok(void)
{
    mem_init(2, 65536);
    ASSERT(mem_select_tape(2) == -1);
    ASSERT(mem_fault() == TOS_HALT_BAD_TAPE);
    mem_clear_fault();
    ASSERT(mem_fault() == 0);
    ASSERT(mem_select_tape(1) == 0);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_selected_tape() == 1);
    return 0;
}

static int t_ws4_01b_fault_stays_pending(void)
{
    mem_init(2, 65536);
    ASSERT(mem_select_tape(2) == -1);
    mem_write(0x0100, 1);
    ASSERT(mem_read(0x0100) == 1);
    ASSERT(mem_fault() == TOS_HALT_BAD_TAPE);   /* in-range accesses do not clear a pending fault */
    return 0;
}

/* ----------------------------------------------------------------- WS4-02a */

static int t_ws4_02a_read_beyond_faults(void)
{
    mem_init(1, 32768);
    ASSERT(mem_tape_len() == 32768);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0x8000) == 0xFF);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    return 0;
}

static int t_ws4_02a_write_beyond_ignored(void)
{
    mem_init(1, 32768);
    mem_set_step(1);
    mem_write(0x8000, 1);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    ASSERT(mem_raw()[0x8000] == 0);
    ASSERT(mem_tape_raw(0)[0x8000] == 0);
    ASSERT(mem_cells_written() == 0);          /* a dropped write writes no cell */
    return 0;
}

static int t_ws4_02a_clear_fault(void)
{
    mem_init(1, 32768);
    ASSERT(mem_read(0x8000) == 0xFF);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    mem_clear_fault();
    ASSERT(mem_fault() == 0);
    mem_write(0x7FFF, 0x42);                   /* last valid address */
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0x7FFF) == 0x42);
    ASSERT(mem_fault() == 0);
    return 0;
}

static int t_ws4_02a_fault_stays_pending(void)
{
    mem_init(1, 32768);
    mem_write(0x8000, 1);
    mem_write(0x0100, 2);
    ASSERT(mem_read(0x0100) == 2);             /* in-range accesses keep working ... */
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT); /* ... and the fault stays until cleared */
    return 0;
}

static int t_ws4_02a_48k_edge(void)
{
    mem_init(1, 49152);
    ASSERT(mem_tape_len() == 49152);
    mem_write(0xBFFF, 0x11);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0xBFFF) == 0x11);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0xC000) == 0xFF);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    mem_clear_fault();
    mem_write(0xC000, 0x22);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    ASSERT(mem_raw()[0xC000] == 0);
    return 0;
}

static int t_ws4_02a_top_of_address_space(void)
{
    mem_init(1, 32768);
    ASSERT(mem_read(0xFFFF) == 0xFF);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    mem_clear_fault();
    mem_write(0xFFFF, 0x33);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    ASSERT(mem_raw()[0xFFFF] == 0);
    return 0;
}

static int t_ws4_02a_64k_never_faults(void)
{
    mem_init(1, 65536);
    mem_write(0xFFFF, 0x44);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0xFFFF) == 0x44);
    ASSERT(mem_read(0x8000) == 0);
    ASSERT(mem_read(0xC000) == 0);
    ASSERT(mem_fault() == 0);
    return 0;
}

static int t_ws4_02a_faulted_read_masks_raw_byte(void)
{
    mem_init(1, 32768);
    mem_poke(0, 0x8000, 0x12);                 /* raw backing byte beyond L, no fault */
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0x8000) == 0xFF);          /* out of range reads 0xFF regardless of the backing byte */
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    return 0;
}

static int t_ws4_02a_tape1_beyond_faults(void)
{
    mem_init(2, 32768);
    ASSERT(mem_select_tape(1) == 0);
    ASSERT(mem_read(0x8000) == 0xFF);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    mem_clear_fault();
    mem_write(0x8000, 0x55);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    ASSERT(mem_tape_raw(1)[0x8000] == 0);
    ASSERT(mem_raw()[0x8000] == 0);
    return 0;
}

static int t_ws4_02a_peek_poke_never_fault(void)
{
    mem_init(1, 32768);
    (void)mem_peek(0, 0x8000);
    ASSERT(mem_fault() == 0);
    mem_poke(0, 0x7FFF, 0x66);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0x7FFF) == 0x66);
    ASSERT(mem_fault() == 0);
    return 0;
}

static int t_ws4_02a_init_clears_fault(void)
{
    mem_init(1, 32768);
    ASSERT(mem_read(0x8000) == 0xFF);
    ASSERT(mem_fault() == TOS_HALT_TAPE_FAULT);
    mem_init(1, 32768);
    ASSERT(mem_fault() == 0);
    ASSERT(mem_read(0x0100) == 0);
    ASSERT(mem_fault() == 0);
    return 0;
}

/* ------------------------------------------------------------------ WS4-08 */

static int t_ws4_08_travel_and_accesses(void)
{
    mem_init(1, 65536);
    (void)mem_read(0x0100);
    mem_write(0x0110, 1);
    (void)mem_read(0x0105);
    ASSERT(mem_travel() == 27);               /* |0x110-0x100| + |0x105-0x110| = 16 + 11 */
    ASSERT(mem_accesses() == 3);
    return 0;
}

static int t_ws4_08_fresh_and_reset(void)
{
    mem_init(1, 65536);
    ASSERT(mem_travel() == 0);
    ASSERT(mem_accesses() == 0);
    ASSERT(mem_cells_written() == 0);
    (void)mem_read(0x0100);
    ASSERT(mem_travel() == 0);                /* first access at 0x0100 adds nothing (per the WS4-08 sum) */
    ASSERT(mem_accesses() == 1);
    mem_init(1, 65536);
    ASSERT(mem_travel() == 0);
    ASSERT(mem_accesses() == 0);
    return 0;
}

static int t_ws4_08_peek_poke_not_counted(void)
{
    mem_init(1, 65536);
    mem_set_step(5);
    (void)mem_peek(0, 0x0100);
    mem_poke(0, 0x0200, 1);
    ASSERT(mem_accesses() == 0);
    ASSERT(mem_travel() == 0);
    ASSERT(mem_write_age(0)[0x0200] == 0);    /* poke has no side effects */
    ASSERT(mem_read_age(0)[0x0100] == 0);
    ASSERT(mem_peek(0, 0x0200) == 1);
    return 0;
}

static int t_ws4_08_cells_written_distinct(void)
{
    mem_init(2, 65536);
    mem_set_step(1);
    mem_write(0x0100, 1);
    mem_write(0x0100, 2);
    mem_write(0x0101, 3);
    ASSERT(mem_cells_written() == 2);
    ASSERT(mem_accesses() == 3);
    ASSERT(mem_travel() == 1);
    /* the same window address on two tapes is two cells */
    mem_write(0x5000, 4);
    ASSERT(mem_select_tape(1) == 0);
    mem_write(0x5000, 5);
    ASSERT(mem_cells_written() == 4);
    return 0;
}

int main(void)
{
    TEST("WS1-03: mem_write at step 7 stamps write age 7", t_ws1_03_write_age);
    TEST("WS1-03: mem_dirty_pages(out,7) marks only page 0x43", t_ws1_03_dirty_pages);
    TEST("WS1-03: mem_read stamps read age with the current step", t_ws1_03_read_age);
    TEST("WS1-03: untouched cells stay at age 0 and later since_step finds nothing dirty", t_ws1_03_untouched_cells);
    TEST("WS1-03: mem_init zero-fills and resets ages and counters", t_ws1_03_init_resets);
    TEST("WS4-01a: write to 0x5000 on tape 1 leaves tape 0 untouched", t_ws4_01a_tape1_isolated);
    TEST("WS4-01a: write to 0x0200 with tape 1 selected lands on tape 0", t_ws4_01a_common_lands_on_tape0);
    TEST("WS4-01a: bank window edges are per-tape, neighbours are common", t_ws4_01a_window_edges);
    TEST("WS4-01b: mem_select_tape(2) with 2 tapes returns -1 and faults BAD_TAPE", t_ws4_01b_two_tapes_select_2);
    TEST("WS4-01b: mem_select_tape(1) with 1 tape returns -1 and faults BAD_TAPE", t_ws4_01b_one_tape_select_1);
    TEST("WS4-01b: mem_select_tape(4) with 4 tapes returns -1 and faults BAD_TAPE", t_ws4_01b_four_tapes_select_4);
    TEST("WS4-01b: mem_select_tape(255) returns -1 and faults BAD_TAPE", t_ws4_01b_select_255);
    TEST("WS4-01b: mem_clear_fault clears BAD_TAPE and a valid select then succeeds", t_ws4_01b_clear_then_ok);
    TEST("WS4-01b: BAD_TAPE stays pending across in-range accesses", t_ws4_01b_fault_stays_pending);
    TEST("WS4-02a: mem_read(0x8000) on 32K returns 0xFF and faults TAPE_FAULT", t_ws4_02a_read_beyond_faults);
    TEST("WS4-02a: mem_write(0x8000,1) on 32K is ignored and faults TAPE_FAULT", t_ws4_02a_write_beyond_ignored);
    TEST("WS4-02a: mem_clear_fault clears TAPE_FAULT; 0x7FFF is still in range", t_ws4_02a_clear_fault);
    TEST("WS4-02a: TAPE_FAULT stays pending across in-range accesses", t_ws4_02a_fault_stays_pending);
    TEST("WS4-02a: 48K tape faults at 0xC000 and not at 0xBFFF", t_ws4_02a_48k_edge);
    TEST("WS4-02a: 0xFFFF on a 32K tape faults on read and write", t_ws4_02a_top_of_address_space);
    TEST("WS4-02a: a 64K tape never faults", t_ws4_02a_64k_never_faults);
    TEST("WS4-02a: faulted read returns 0xFF even when the raw byte is non-zero", t_ws4_02a_faulted_read_masks_raw_byte);
    TEST("WS4-02a: beyond-L access with tape 1 selected faults and is dropped", t_ws4_02a_tape1_beyond_faults);
    TEST("WS4-02a: mem_peek/mem_poke never set a fault", t_ws4_02a_peek_poke_never_fault);
    TEST("WS4-02a: mem_init after a fault leaves mem_fault()==0", t_ws4_02a_init_clears_fault);
    TEST("WS4-08: travel after 0x0100,0x0110,0x0105 is 27 and accesses is 3", t_ws4_08_travel_and_accesses);
    TEST("WS4-08: fresh mem has zero travel/accesses and mem_init resets them", t_ws4_08_fresh_and_reset);
    TEST("WS4-08: mem_peek/mem_poke are not counted as accesses", t_ws4_08_peek_poke_not_counted);
    TEST("WS4-08: mem_cells_written counts distinct (tape,addr) cells", t_ws4_08_cells_written_distinct);
    printf("PASS: test_v2_mem\n");
    RUN_ALL_TESTS();
}
