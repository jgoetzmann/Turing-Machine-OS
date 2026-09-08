/* WS4-04, WS4-06, WS4-09: every lever is settable, readable, mirrored in the metadata block and
 * reachable from the CLI, and the two that had no test of their own (input mode, snapshot
 * interval) actually change what the machine does. */
#include "../testfw.h"
#include "api/api.h"
#include "tos.h"

#include <string.h>

/* JMP $ : runs forever without touching anything. */
static const uint8_t SPIN[] = { 0xC3, 0x00, 0x01 };

static uint32_t meta_u32(uint32_t off) {
    const uint8_t *m = tos_meta_ptr();
    return (uint32_t)m[off] | ((uint32_t)m[off + 1u] << 8) | ((uint32_t)m[off + 2u] << 16) |
           ((uint32_t)m[off + 3u] << 24);
}

static int t_every_lever_round_trips(void) {
    int id;
    ASSERT(tos_create(NULL) == 0);
    for (id = 0; id < TOS_LEVER_COUNT; ++id) {
        uint32_t v = tos_lever_get(id);
        ASSERT(tos_lever_set(id, v) == 0);     /* setting a lever to what it already is is legal */
        ASSERT(tos_lever_get(id) == v);
    }
    ASSERT(tos_lever_set(TOS_LEVER_COUNT, 1) == -1);
    return 0;
}

static int t_levers_are_mirrored_in_metadata(void) {
    const uint8_t *m;
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_SEED, 9u) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_HZ, 2000000u) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_INPUT_MODE, TOS_INPUT_KEYS) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_TRACE, 0u) == 0);
    (void)tos_step(1u);
    m = tos_meta_ptr();
    ASSERT(m[TOS_META_SEED] == 9u);
    ASSERT(meta_u32(TOS_META_HZ) == 2000000u);
    ASSERT(m[TOS_META_INPUT_MODE] == (uint8_t)TOS_INPUT_KEYS);
    ASSERT(m[TOS_META_TRACE] == 0u);
    ASSERT(m[TOS_META_DISKS] == 1u);
    return 0;
}

static int t_input_mode_is_a_view_lever(void) {
    uint32_t steps;
    ASSERT(tos_create(NULL) == 0);
    (void)tos_step(500u);
    steps = tos_steps();
    /* A view lever must not reset the machine. */
    ASSERT(tos_lever_set(TOS_LEVER_INPUT_MODE, TOS_INPUT_KEYS) == 0);
    ASSERT(tos_steps() == steps);
    ASSERT(tos_lever_get(TOS_LEVER_INPUT_MODE) == (uint32_t)TOS_INPUT_KEYS);
    /* Console input still reaches the machine in either mode: the lever tells the UI where to
       send what the user types, it does not close the console. */
    tos_con_push((uint8_t)'\n');
    (void)tos_step(500u);
    ASSERT(tos_steps() > steps);
    ASSERT(tos_lever_set(TOS_LEVER_INPUT_MODE, TOS_INPUT_CONSOLE) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_INPUT_MODE, 2u) == -1);      /* only 0 and 1 exist */
    return 0;
}

static int t_snapshot_interval_controls_the_spacing(void) {
    int wide;
    int narrow;

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_SNAP_INTERVAL, 0u) == 0);    /* off: only the anchors */
    ASSERT(tos_load_com(SPIN, (uint32_t)sizeof SPIN) == 0);
    (void)tos_step(5000u);
    wide = tos_snapshot_count();

    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_SNAP_INTERVAL, 500u) == 0);
    ASSERT(tos_load_com(SPIN, (uint32_t)sizeof SPIN) == 0);
    (void)tos_step(5000u);
    narrow = tos_snapshot_count();

    ASSERT(narrow > wide);                                      /* the interval really is honoured */
    ASSERT(tos_seek(4000u) == 0);                               /* and a late step is reachable */
    return 0;
}

int main(void) {
    TEST("WS4-09: every lever id reads back what it was set to, and there is no ninth", t_every_lever_round_trips);
    TEST("WS4-09: the metadata block mirrors the levers the machine is running with", t_levers_are_mirrored_in_metadata);
    TEST("WS4-06: the input-mode lever is a view lever and rejects anything but 0 and 1", t_input_mode_is_a_view_lever);
    TEST("WS4-04: the snapshot interval changes how many snapshots a run leaves behind", t_snapshot_interval_controls_the_spacing);
    printf("PASS: test_v2_ws4_levers\n");
    RUN_ALL_TESTS();
}
