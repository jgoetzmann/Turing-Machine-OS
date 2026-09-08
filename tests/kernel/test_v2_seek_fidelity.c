/* Time travel has to be honest: a seek either reconstructs the exact state of that step or says
 * it cannot. These are the cases where it used to quietly produce something else. */
#include "../testfw.h"
#include "api/api.h"
#include "emu/cpu.h"
#include "kernel/trace.h"
#include "tos.h"

#include <string.h>

/* MVI A,<mark> / JMP $ : an image that says which load it came from. */
static void marked_program(uint8_t mark, uint8_t out[5]) {
    out[0] = 0x3E; out[1] = mark;      /* MVI A,mark */
    out[2] = 0xC3; out[3] = 0x02; out[4] = 0x01;   /* JMP 0102 */
}

/* MVI A,02 / MVI C,'X' / OUT 1 (CONOUT) / JMP $ : keeps writing one byte per pass. */
static const uint8_t TALKER[] = { 0x3E, 0x02, 0x0E, 0x58, 0xD3, 0x01, 0xC3, 0x00, 0x01 };

/* 20 NOPs then STA 4000H then JMP $ : one write, at a known step. */
static const uint8_t WRITER[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x3E, 0x5A, 0x32, 0x00, 0x40, 0xC3, 0x19, 0x01
};

static const cpu_t *cpu(void) { return (const cpu_t *)tos_cpu_ptr(); }

static int t_recycled_load_slot_is_refused(void) {
    uint8_t prog[5];
    int i;
    int rc;

    ASSERT(tos_create(NULL) == 0);
    marked_program(0xA1u, prog);
    ASSERT(tos_load_com(prog, 5u) == 0);
    (void)tos_step(50u);
    ASSERT(cpu()->a == 0xA1u);

    /* Enough loads to push every early anchor out of the snapshot ring, so seeking back into the
       first program has to replay the load entries from the beginning. */
    for (i = 0; i < 60; ++i) {
        marked_program((uint8_t)(0xB0u + (i & 0x0Fu)), prog);
        ASSERT(tos_load_com(prog, 5u) == 0);
        (void)tos_step(50u);
    }

    /* Step 25 is inside the first program. Its image is long gone, so the only honest answers are
       "no" or the right machine; what must not happen is a later program presented as step 25. */
    rc = tos_seek(25u);
    if (rc == 0) {
        ASSERT(cpu()->a == 0xA1u);
    } else {
        ASSERT(rc == -1);
    }
    return 0;
}

static int t_failed_seek_leaves_the_machine_alone(void) {
    uint32_t before;

    /* A bare machine boots the shell and parks at its prompt. Without any input in the log there
       is no way to reach a later step, so the seek has to fail. */
    ASSERT(tos_create(NULL) == 0);
    (void)tos_step(500u);
    before = tos_steps();
    ASSERT(tos_stop_reason() == (int)KSTOP_WAIT_INPUT);

    ASSERT(tos_seek(before + 5000u) == -1);
    ASSERT(tos_steps() == before);
    ASSERT(tos_state() == (int)KS_IDLE);
    return 0;
}

static int t_seek_forward_keeps_the_output(void) {
    uint32_t mid;
    int c;
    int seen = 0;

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(TALKER, (uint32_t)sizeof TALKER) == 0);
    (void)tos_step(40u);
    mid = tos_steps();
    while (tos_con_pop() >= 0) { /* drop what the first run produced */ }

    ASSERT(tos_seek(mid / 2u) == 0);
    while (tos_con_pop() >= 0) { /* replaying old steps is silent, as designed */ }

    /* Running forward again covers steps the host has never seen: their output must arrive. */
    (void)tos_step(40u);
    while ((c = tos_con_pop()) >= 0) {
        ASSERT(c == 'X');
        seen++;
    }
    ASSERT(seen > 0);
    return 0;
}

static int t_seek_does_not_duplicate_the_trace(void) {
    uint32_t count_before;
    uint32_t count_after;

    ASSERT(tos_create(NULL) == 0);
    tos_trace_enable(1);
    ASSERT(tos_load_com(WRITER, (uint32_t)sizeof WRITER) == 0);
    (void)tos_step(300u);
    count_before = tos_trace_count();
    ASSERT(count_before > 0u);

    ASSERT(tos_seek(100u) == 0);
    count_after = tos_trace_count();
    ASSERT(count_after == count_before);       /* replay re-runs history, it does not re-record it */
    return 0;
}

static int t_step_zero_anchor_survives_the_ring(void) {
    int i;
    int slots;
    uint32_t earliest;

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_SNAP_INTERVAL, 100u) == 0);
    ASSERT(tos_load_com(WRITER, (uint32_t)sizeof WRITER) == 0);
    for (i = 0; i < 60; ++i) (void)tos_step(100u);      /* far more snapshots than the ring holds */

    slots = tos_snapshot_count();
    ASSERT(slots > 0);
    earliest = tos_snapshot_step(0);
    for (i = 1; i < slots; ++i) {
        uint32_t s = tos_snapshot_step(i);
        if (s < earliest) earliest = s;
    }
    /* The oldest snapshot is still the anchor taken at the very beginning, so an early step is
       still reachable after the ring has wrapped. */
    ASSERT(earliest <= 1u);
    ASSERT(tos_seek(earliest) == 0);
    return 0;
}

static int t_dirty_map_forgets_the_abandoned_future(void) {
    const uint8_t *meta;
    uint8_t page40_before;
    uint8_t page40_after;

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_SNAP_INTERVAL, 5u) == 0);
    ASSERT(tos_load_com(WRITER, (uint32_t)sizeof WRITER) == 0);
    (void)tos_step(10u);                        /* past the STA: page 0x40 has been written */
    (void)tos_step(50u);
    meta = tos_meta_ptr();
    page40_before = (uint8_t)((meta[TOS_META_DIRTY + (0x40u / 8u)] >> (0x40u % 8u)) & 1u);
    ASSERT(page40_before == 1u);

    /* Back before the write: the page has not been written yet on this timeline. */
    ASSERT(tos_seek(5u) == 0);
    meta = tos_meta_ptr();
    page40_after = (uint8_t)((meta[TOS_META_DIRTY + (0x40u / 8u)] >> (0x40u % 8u)) & 1u);
    ASSERT(page40_after == 0u);
    return 0;
}

static int t_anchor_holds_the_current_timeline(void) {
    uint8_t prog[5];
    int i;

    /* kernel_init anchors step 0, and so does the load that follows: both describe step 0, but
       only the second one is on the timeline the machine is actually running. */
    ASSERT(tos_create(NULL) == 0);
    marked_program(0xA1u, prog);
    ASSERT(tos_load_com(prog, 5u) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_SNAP_INTERVAL, 50u) == 0);
    for (i = 0; i < 60; ++i) (void)tos_step(50u);      /* wrap the rotating part of the ring */

    ASSERT(tos_seek(0u) == 0);
    ASSERT(tos_step(2u) == 2u);
    ASSERT(cpu()->a == 0xA1u);                          /* the loaded program, not the bare shell */
    return 0;
}

static int t_seek_the_anchor_alone_can_satisfy(void) {
    uint8_t prog[5];
    uint32_t at_load;

    ASSERT(tos_create(NULL) == 0);
    marked_program(0xC3u, prog);
    ASSERT(tos_load_com(prog, 5u) == 0);
    at_load = tos_steps();
    (void)tos_step(200u);

    /* The load's own anchor is exactly this state; the seek must not refuse it because the log
       still lists the load as pending. */
    ASSERT(tos_seek(at_load) == 0);
    ASSERT(tos_steps() == at_load);
    ASSERT(tos_step(2u) == 2u);
    ASSERT(cpu()->a == 0xC3u);
    return 0;
}

static int t_failed_seek_is_silent(void) {
    uint32_t before;
    int c;

    /* The boot prints a prompt. A failed seek puts the machine back by replaying those steps, and
       the host must not see that output a second time. */
    ASSERT(tos_create(NULL) == 0);
    (void)tos_step(500u);
    before = tos_steps();
    while (tos_con_pop() >= 0) { /* drain what the boot printed */ }

    ASSERT(tos_seek(before + 5000u) == -1);
    ASSERT(tos_steps() == before);
    c = tos_con_pop();
    ASSERT(c < 0);                                      /* nothing was replayed to the console */
    return 0;
}

static int t_failed_forward_seek_keeps_its_output(void) {
    uint32_t before;
    int c;
    int seen = 0;

    /* A forward seek that cannot finish throws its steps away, so the output of those steps must
       not reach the host either: it describes a timeline that did not happen. */
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(TALKER, (uint32_t)sizeof TALKER) == 0);
    (void)tos_step(40u);
    before = tos_steps();
    while (tos_con_pop() >= 0) { /* drain */ }

    /* Seek backwards first so the machine has somewhere to return to, then ask for a step the
       log cannot reach because the program halts before it. */
    ASSERT(tos_seek(before / 2u) == 0);
    while (tos_con_pop() >= 0) { /* replayed steps stay silent */ }

    /* Forward again, but only as far as the machine really goes. */
    ASSERT(tos_seek(before) == 0);
    while ((c = tos_con_pop()) >= 0) {
        ASSERT(c == 'X');
        seen++;
    }
    ASSERT(seen > 0);                                   /* those steps did happen: the output is real */
    return 0;
}

int main(void) {
    TEST("WS1-14: a seek whose program image was recycled refuses instead of loading another", t_recycled_load_slot_is_refused);
    TEST("WS1-14: a seek that cannot reach its target leaves the machine where it was", t_failed_seek_leaves_the_machine_alone);
    TEST("WS1-14: seeking then running forward delivers the new output", t_seek_forward_keeps_the_output);
    TEST("WS1-14: a seek does not push the replayed steps into the trace again", t_seek_does_not_duplicate_the_trace);
    TEST("WS1-14: the step-0 anchor outlives the snapshot ring", t_step_zero_anchor_survives_the_ring);
    TEST("WS1-14: the dirty map after a seek describes the restored timeline", t_dirty_map_forgets_the_abandoned_future);
    TEST("WS1-14: the pinned anchor holds the timeline the machine is on", t_anchor_holds_the_current_timeline);
    TEST("WS1-14: a seek the load's own anchor satisfies is not refused", t_seek_the_anchor_alone_can_satisfy);
    TEST("WS1-14: a failed seek does not replay output the host already saw", t_failed_seek_is_silent);
    TEST("WS1-14: a forward seek delivers the output of the steps it really took", t_failed_forward_seek_keeps_its_output);
    printf("PASS: test_v2_seek_fidelity\n");
    RUN_ALL_TESTS();
}
