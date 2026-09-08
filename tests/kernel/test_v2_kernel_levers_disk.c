/* WS4-03, WS4-05, WS4-07, WS5-05: levers, BIOS PRNG, disks, framebuffer.
 * Drives the machine through src/api/api.h; WS4-05 also calls the BIOS PRNG directly. */
#include "../testfw.h"
#include "api/api.h"
#include "bios/bios.h"
#include "hal/hal.h"
#include <string.h>

#define MAX_STEPS 2000000u

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

/* Steps in chunks until tape 0 byte `addr` reads `val` (a done flag poked by the program). */
static int run_until_flag(uint16_t addr, uint8_t val, uint32_t budget) {
    uint32_t total = 0;
    while (total < budget) {
        if (tos_tape_ptr(0)[addr] == val) return 0;
        total += tos_step(1024);
        if (tos_stop_reason() != KSTOP_BUDGET) return -1;
    }
    return tos_tape_ptr(0)[addr] == val ? 0 : -1;
}

static uint32_t meta_u32(unsigned off) {
    const uint8_t *m = tos_meta_ptr();
    return (uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) | ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24);
}
static unsigned meta_u16(unsigned off) {
    const uint8_t *m = tos_meta_ptr();
    return (unsigned)m[off] | ((unsigned)m[off + 1] << 8);
}

static uint8_t com[16384];
static char err[256];
static const uint8_t SPIN[] = { 0xC3, 0x00, 0x01 };            /* 0100: JMP 0100H */

/* ---- WS4-03 ----------------------------------------------------------- */
static int t_hz_lever_no_sleep(void) {
    tos_config_t cfg;
    uint32_t t0;
    uint32_t ms;
    uint32_t s0, c0;
    kernel_config_default(&cfg);
    ASSERT(tos_create(&cfg) == 0);
    ASSERT(tos_lever_get(TOS_LEVER_HZ) == 0u);
    ASSERT(tos_load_com(SPIN, (uint32_t)sizeof SPIN) == 0);
    ASSERT(tos_step(10) == 10u);
    s0 = tos_steps();
    ASSERT(tos_lever_set(TOS_LEVER_HZ, 2000000u) == 0);
    ASSERT(tos_lever_get(TOS_LEVER_HZ) == 2000000u);
    ASSERT(tos_steps() == s0);                                  /* view lever: no machine reset */
    ASSERT(tos_step(1) == 1u);
    ASSERT(meta_u32(TOS_META_HZ) == 2000000u);
    c0 = tos_cycles_lo();
    /* Wall time, not processor time: a kernel that slept would burn no CPU and pass a clock() test. */
    t0 = hal_time_ms();
    ASSERT(run_bounded(40000u) == KSTOP_BUDGET);                /* 40,000 x JMP = 400,000 cycles; 200 ms at 2 MHz if throttled */
    ms = hal_time_ms() - t0;
    ASSERT(tos_cycles_lo() - c0 >= 400000u);
    ASSERT(ms < 100u);
    ASSERT(tos_steps() == s0 + 1u + 40000u);
    return 0;
}

static int t_lever_bad_id(void) {
    uint32_t before;
    ASSERT(tos_create(NULL) == 0);
    before = tos_lever_get(TOS_LEVER_HZ);
    ASSERT(tos_lever_set(-1, 1) == -1);
    ASSERT(tos_lever_set(TOS_LEVER_COUNT, 1) == -1);
    ASSERT(tos_lever_set(99, 1) == -1);
    ASSERT(tos_lever_set(1000, 0) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_HZ) == before);
    return 0;
}

static int t_lever_bad_value(void) {
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_lever_set(TOS_LEVER_TAPES, 3) == -1);
    ASSERT(tos_lever_set(TOS_LEVER_TAPES, 0) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_TAPES) == 1u);
    ASSERT(tos_tape_count() == 1);
    ASSERT(tos_lever_set(TOS_LEVER_TAPE_LEN, 1234) == -1);
    ASSERT(tos_lever_set(TOS_LEVER_TAPE_LEN, 0) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_TAPE_LEN) == 65536u);
    ASSERT(tos_tape_len() == 65536u);
    ASSERT(tos_lever_set(TOS_LEVER_SEED, 256) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_SEED) == 1u);
    ASSERT(tos_lever_set(TOS_LEVER_INPUT_MODE, 2) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_INPUT_MODE) == 0u);
    ASSERT(tos_lever_set(TOS_LEVER_DISKS, 3) == -1);
    ASSERT(tos_lever_set(TOS_LEVER_DISKS, 0) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_DISKS) == 1u);
    ASSERT(tos_lever_set(TOS_LEVER_TRACE, 2) == -1);
    ASSERT(tos_lever_get(TOS_LEVER_TRACE) == 0u);
    return 0;
}

static int t_machine_levers_reset_and_report(void) {
    tos_config_t cfg;
    kernel_config_default(&cfg);
    cfg.tapes = 2; cfg.tape_len = 49152u; cfg.seed = 9; cfg.input_mode = TOS_INPUT_KEYS;
    cfg.disks = 2; cfg.trace = 1; cfg.snap_interval = 500;
    ASSERT(tos_create(&cfg) == 0);
    ASSERT(tos_lever_get(TOS_LEVER_TAPES) == 2u);
    ASSERT(tos_lever_get(TOS_LEVER_TAPE_LEN) == 49152u);
    ASSERT(tos_lever_get(TOS_LEVER_HZ) == 0u);
    ASSERT(tos_lever_get(TOS_LEVER_SEED) == 9u);
    ASSERT(tos_lever_get(TOS_LEVER_INPUT_MODE) == (uint32_t)TOS_INPUT_KEYS);
    ASSERT(tos_lever_get(TOS_LEVER_DISKS) == 2u);
    ASSERT(tos_lever_get(TOS_LEVER_TRACE) == 1u);
    ASSERT(tos_lever_get(TOS_LEVER_SNAP_INTERVAL) == 500u);
    /* a machine lever recreates the machine with the new value, keeping the others */
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(SPIN, (uint32_t)sizeof SPIN) == 0);
    ASSERT(tos_step(100) == 100u);
    ASSERT(tos_lever_set(TOS_LEVER_TAPES, 2) == 0);
    ASSERT(tos_lever_get(TOS_LEVER_TAPES) == 2u);
    ASSERT(tos_tape_count() == 2);
    ASSERT(tos_steps() == 0u);
    ASSERT(tos_state() == KS_SHELL);
    ASSERT(tos_lever_set(TOS_LEVER_TAPE_LEN, 32768u) == 0);
    ASSERT(tos_tape_len() == 32768u);
    ASSERT(tos_tape_count() == 2);
    ASSERT(tos_lever_set(TOS_LEVER_SEED, 7) == 0);
    ASSERT(tos_lever_get(TOS_LEVER_SEED) == 7u);
    ASSERT(tos_step(1) == 1u);
    ASSERT(tos_meta_ptr()[TOS_META_TAPE_COUNT] == 2);
    ASSERT(meta_u16(TOS_META_TAPE_PAGES) == 0x0080u);
    ASSERT(meta_u16(TOS_META_SP_INIT) == TOS_STACK_TOP(32768u));
    ASSERT(tos_meta_ptr()[TOS_META_SEED] == 7);
    return 0;
}

/* ---- WS4-05 ----------------------------------------------------------- */
/* Reference: bios.h's 8-bit xorshift (x^=x<<3; x^=x>>5; x^=x<<1). */
static void ref_xorshift(uint8_t seed, uint8_t out[4]) {
    uint8_t x = seed ? seed : 1;
    int i;
    for (i = 0; i < 4; i++) {
        x ^= (uint8_t)(x << 3);
        x ^= (uint8_t)(x >> 5);
        x ^= (uint8_t)(x << 1);
        out[i] = x;
    }
}

/* The spec fixes the generator but not whether the returned byte is pre- or post-advance. */
static int matches_xorshift(const uint8_t r[4], uint8_t seed) {
    uint8_t post[4], pre[4];
    ref_xorshift(seed, post);
    pre[0] = seed ? seed : 1; pre[1] = post[0]; pre[2] = post[1]; pre[3] = post[2];
    return memcmp(r, post, 4) == 0 || memcmp(r, pre, 4) == 0;
}

static void bios_seq(uint8_t seed, uint8_t out[4]) {
    int i;
    bios_init();
    bios_set_seed(seed);
    for (i = 0; i < 4; i++) out[i] = bios_rand();
}

static const char RAND_SRC[] =
    "int main() {\n"
    "    poke(0x4000, rand());\n"
    "    poke(0x4001, rand());\n"
    "    poke(0x4002, rand());\n"
    "    poke(0x4003, rand());\n"
    "    poke(0x4004, 90);\n"
    "    while (1) ;\n"
    "    return 0;\n"
    "}\n";

static int run_rand_program(uint8_t seed, uint8_t out[4]) {
    tos_config_t cfg;
    int n;
    kernel_config_default(&cfg);
    cfg.seed = seed;
    if (tos_create(&cfg) != 0) return -1;
    n = tos_compile(TOS_LANG_C, RAND_SRC, (uint32_t)(sizeof RAND_SRC - 1), com, (uint32_t)sizeof com, err, (uint32_t)sizeof err);
    if (n <= 0) return -2;
    if (tos_load_com(com, (uint32_t)n) != 0) return -3;
    if (run_until_flag(0x4004, 90, 200000u) != 0) return -4;
    memcpy(out, tos_tape_ptr(0) + 0x4000, 4);
    return 0;
}

static int t_bios_rand_deterministic(void) {
    uint8_t a[4], b[4];
    bios_seq(1, a);
    bios_seq(1, b);
    ASSERT(memcmp(a, b, 4) == 0);
    ASSERT(matches_xorshift(a, 1));
    ASSERT(!(a[0] == a[1] && a[1] == a[2] && a[2] == a[3]));
    return 0;
}

static int t_tinyc_rand_matches_bios(void) {
    uint8_t a[4], b[4];
    bios_seq(1, a);
    ASSERT(run_rand_program(1, b) == 0);
    ASSERT(memcmp(a, b, 4) == 0);
    return 0;
}

static int t_seed_zero_behaves_as_one(void) {
    uint8_t one[4], zero[4], c1[4], c0[4];
    bios_seq(1, one);
    bios_seq(0, zero);
    ASSERT(memcmp(one, zero, 4) == 0);
    ASSERT(run_rand_program(1, c1) == 0);
    ASSERT(run_rand_program(0, c0) == 0);
    ASSERT(memcmp(c1, c0, 4) == 0);
    ASSERT(memcmp(c1, one, 4) == 0);
    return 0;
}

static int t_seed_two_differs(void) {
    uint8_t one[4], two[4], c2[4];
    bios_seq(1, one);
    bios_seq(2, two);
    ASSERT(memcmp(one, two, 4) != 0);
    ASSERT(matches_xorshift(two, 2));
    ASSERT(run_rand_program(2, c2) == 0);
    ASSERT(memcmp(c2, two, 4) == 0);
    ASSERT(memcmp(c2, one, 4) != 0);
    return 0;
}

/* ---- WS4-07 ----------------------------------------------------------- */
static const uint8_t HELLO[] = { 'h', 'e', 'l', 'l', 'o' };

static int create_disks(uint8_t disks) {
    tos_config_t cfg;
    kernel_config_default(&cfg);
    cfg.disks = disks;
    return tos_create(&cfg);
}

static int t_disk_put_list(void) {
    char list[2048];
    uint8_t back[16];
    ASSERT(create_disks(2) == 0);
    ASSERT(tos_disk_put_file(1, "B.TXT", HELLO, 5) == 0);
    memset(list, 0, sizeof list);
    ASSERT(tos_disk_list(1, list, (uint32_t)sizeof list) == 1);
    ASSERT(strstr(list, "B.TXT\n") != NULL);
    memset(list, 0, sizeof list);
    ASSERT(tos_disk_list(0, list, (uint32_t)sizeof list) == 0);
    ASSERT(strstr(list, "B.TXT") == NULL);
    ASSERT(tos_disk_get_file(1, "B.TXT", back, (uint32_t)sizeof back) == 5);
    ASSERT(memcmp(back, HELLO, 5) == 0);
    return 0;
}

static int t_disk_get_missing(void) {
    uint8_t back[16];
    ASSERT(create_disks(2) == 0);
    ASSERT(tos_disk_put_file(1, "B.TXT", HELLO, 5) == 0);
    ASSERT(tos_disk_get_file(0, "B.TXT", back, (uint32_t)sizeof back) == -1);   /* other disk */
    ASSERT(tos_disk_get_file(1, "NOPE.TXT", back, (uint32_t)sizeof back) == -1);
    return 0;
}

static int t_disk_bad_index(void) {
    char list[2048];
    uint8_t back[16];
    ASSERT(create_disks(1) == 0);
    ASSERT(tos_disk_put_file(1, "B.TXT", HELLO, 5) != 0);      /* disk 1 does not exist */
    ASSERT(tos_disk_get_file(1, "B.TXT", back, (uint32_t)sizeof back) == -1);
    memset(list, 0, sizeof list);
    ASSERT(tos_disk_list(0, list, (uint32_t)sizeof list) == 0); /* nothing leaked onto disk 0 */
    ASSERT(create_disks(2) == 0);
    ASSERT(tos_disk_put_file(2, "B.TXT", HELLO, 5) != 0);      /* TOS_DISKS_MAX is 2 */
    ASSERT(tos_disk_get_file(2, "B.TXT", back, (uint32_t)sizeof back) == -1);
    return 0;
}

static int t_shell_disk_b_dir(void) {
    char out[4096];
    ASSERT(create_disks(2) == 0);
    ASSERT(tos_disk_put_file(1, "B.TXT", HELLO, 5) == 0);
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "A> ") != NULL);
    push_str("dir\n");                                          /* disk A: nothing there */
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "(empty)") != NULL);
    ASSERT(strstr(out, "B.TXT") == NULL);
    push_str("disk b\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "B> ") != NULL);
    push_str("dir\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_WAIT_INPUT);
    drain_out(out, sizeof out);
    ASSERT(strstr(out, "B.TXT\n") != NULL);
    ASSERT(strstr(out, "B> ") != NULL);
    ASSERT(strstr(out, "(empty)") == NULL);
    push_str("halt\n");
    ASSERT(run_bounded(MAX_STEPS) == KSTOP_HALT);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_COMMAND);
    return 0;
}

/* ---- WS5-05 ----------------------------------------------------------- */
/* LXI H,base ; MVI B,0 ; loop: MVI M,AAH ; INX H ; DCR B ; JNZ loop ; HLT   (256 bytes, 1027 instructions) */
static const uint8_t FILL64[] = { 0x21, 0x00, 0xFE, 0x06, 0x00, 0x36, 0xAA, 0x23, 0x05, 0xC2, 0x05, 0x01, 0x76 };
static const uint8_t FILL32[] = { 0x21, 0x00, 0x7E, 0x06, 0x00, 0x36, 0xAA, 0x23, 0x05, 0xC2, 0x05, 0x01, 0x76 };
#define FILL_STEPS 1027u
/* LXI H,FE00H ; LXI B,0200H ; loop: MVI M,AAH ; INX H ; DCX B ; MOV A,B ; ORA C ; JNZ loop ; HLT (512 bytes, 3075 instr.) */
static const uint8_t FILL512[] = { 0x21, 0x00, 0xFE, 0x01, 0x00, 0x02, 0x36, 0xAA, 0x23, 0x0B, 0x78, 0xB1, 0xC2, 0x06, 0x01, 0x76 };
#define FILL512_STEPS 3075u

static int check_display_aa(uint32_t tape_len, const uint8_t *prog, uint32_t len) {
    tos_config_t cfg;
    const uint8_t *fb;
    unsigned i;
    kernel_config_default(&cfg);
    cfg.tape_len = tape_len;
    ASSERT(tos_create(&cfg) == 0);
    ASSERT(tos_load_com(prog, len) == 0);
    ASSERT(tos_step(FILL_STEPS) == FILL_STEPS);                 /* ends right after the program's HLT */
    ASSERT(tos_stop_reason() == KSTOP_BUDGET);
    ASSERT(tos_state() == KS_SHELL);
    ASSERT(tos_meta_ptr()[TOS_META_STATE] == KS_SHELL);
    ASSERT(tos_transition_fired(5) >= 1u);
    fb = tos_tape_ptr(0) + TOS_DISPLAY_BASE(tape_len);
    for (i = 0; i < TOS_DISPLAY_SIZE; i++) ASSERT(fb[i] == 0xAA);
    ASSERT(tos_tape_ptr(0)[TOS_DISPLAY_BASE(tape_len) - 1u] == 0);
    ASSERT(tos_halt_reason() == (int)TOS_HALT_NONE);
    return 0;
}

static int t_display_aa_64k(void) { return check_display_aa(65536u, FILL64, (uint32_t)sizeof FILL64); }
static int t_display_aa_32k(void) { return check_display_aa(32768u, FILL32, (uint32_t)sizeof FILL32); }

static int t_meta_rewritten_after_program(void) {
    const uint8_t *fb;
    unsigned i;
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(FILL512, (uint32_t)sizeof FILL512) == 0);
    ASSERT(tos_step(FILL512_STEPS) == FILL512_STEPS);
    ASSERT(tos_state() == KS_SHELL);
    /* the program sprayed 0xAA over the metadata block too; the kernel rewrote it at the end of the call */
    ASSERT(tos_meta_ptr()[TOS_META_STATE] != 0xAA);
    ASSERT(tos_meta_ptr()[TOS_META_STATE] == KS_SHELL);
    ASSERT(meta_u32(TOS_META_STEPS) == tos_steps());
    ASSERT(tos_meta_ptr()[TOS_META_TAPE_COUNT] == 1);
    ASSERT(meta_u16(TOS_META_TAPE_PAGES) == 0x0100u);
    ASSERT(tos_meta_ptr()[TOS_META_HALT_REASON] == (int)TOS_HALT_NONE);
    fb = tos_tape_ptr(0) + TOS_DISPLAY_BASE(65536u);
    for (i = 0; i < TOS_DISPLAY_SIZE; i++) ASSERT(fb[i] == 0xAA);
    return 0;
}

int main(void) {
    TEST("WS4-03: TOS_LEVER_HZ=2000000 is reported by tos_lever_get and TOS_META_HZ; tos_step never sleeps", t_hz_lever_no_sleep);
    TEST("WS4-03: tos_lever_set with a bad lever id returns -1", t_lever_bad_id);
    TEST("WS4-03: tos_lever_set with an out-of-range value returns -1 and keeps the old value", t_lever_bad_value);
    TEST("WS4-03: machine levers recreate the machine; tos_lever_get and the meta block report them", t_machine_levers_reset_and_report);
    TEST("WS4-05: bios_rand from seed 1 is the same xorshift sequence across two BIOS inits", t_bios_rand_deterministic);
    TEST("WS4-05: tiny-C rand() returns the BIOS sequence", t_tinyc_rand_matches_bios);
    TEST("WS4-05: seed 0 behaves as seed 1 (BIOS and tiny-C)", t_seed_zero_behaves_as_one);
    TEST("WS4-05: seed 2 gives a different sequence than seed 1", t_seed_two_differs);
    TEST("WS4-07: tos_disk_put_file(1,B.TXT) is listed by disk 1 and not by disk 0", t_disk_put_list);
    TEST("WS4-07: tos_disk_get_file returns -1 on the other disk or for a missing name", t_disk_get_missing);
    TEST("WS4-07: disk index >= disk count is rejected by put/get", t_disk_bad_index);
    TEST("WS4-07: shell `disk b` then `dir` lists B.TXT under a `B> ` prompt", t_shell_disk_b_dir);
    TEST("WS5-05: a program filling the 64K display with 0xAA leaves KS_SHELL and 256 x 0xAA", t_display_aa_64k);
    TEST("WS5-05: the same program aimed at TOS_DISPLAY_BASE(32768) works on a 32K tape", t_display_aa_32k);
    TEST("WS5-05: a program that sprays the meta block does not survive the kernel's rewrite", t_meta_rewritten_after_program);
    printf("PASS: test_v2_kernel_levers_disk\n");
    RUN_ALL_TESTS();
}
