/* WS6-02 Pong, WS6-03 Life golden, WS6-08 BF cells on tape 1, WS6-05 head-travel ratio — through the API. */
#include "../testfw.h"
#include "../../src/api/api.h"
#include "../../src/lang/bf.h"
#include "../../src/lang/tm.h"
#include <stdio.h>
#include <string.h>

static char g_src[32768];
static uint8_t g_img[16384];
static char g_err[256];

static int read_file(const char *p) {
    FILE *f = fopen(p, "rb"); int n;
    if (f == NULL) return -1;
    n = (int)fread(g_src, 1u, sizeof g_src - 1u, f); fclose(f); g_src[n] = 0; return n;
}
static int fresh(uint8_t tapes) {
    tos_config_t c; kernel_config_default(&c); c.tapes = tapes; c.trace = 0; c.snap_interval = 0;
    return tos_create(&c);
}
/* Run until `frames` VSYNCs (or the program returns to the shell). Returns frames seen; *maxper = max steps in one frame. */
static int run_frames(int frames, uint8_t keys, int hold, int *maxper) {
    int vs = 0, i; uint32_t before = tos_steps(); *maxper = 0;
    tos_keys_set(keys);
    for (i = 0; i < 400000 && vs < frames; i++) {
        (void)tos_step(50000u);
        if (tos_stop_reason() == KSTOP_VSYNC) {
            int per = (int)(tos_steps() - before); if (per > *maxper) *maxper = per; before = tos_steps();
            vs++; if (vs == hold) tos_keys_set(0);
        }
        if (tos_stop_reason() == KSTOP_HALT || tos_state() == KS_SHELL) break;
    }
    return vs;
}

static int t_pong(void) {
    int n, len, maxper, i, nz = 0; const uint8_t *fb;
    ASSERT(fresh(1u) == 0);
    n = read_file("demos/pong/pong.c"); ASSERT(n > 0);
    len = tos_compile(TOS_LANG_C, g_src, (uint32_t)n, g_img, sizeof g_img, g_err, sizeof g_err);
    ASSERT(len > 0 && len <= 16128);
    ASSERT(tos_load_com(g_img, (uint32_t)len) == 0);
    ASSERT(run_frames(600, TOS_KEY_W, 300, &maxper) == 600);
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    ASSERT(maxper <= 6000);
    fb = tos_tape_ptr(0) + TOS_DISPLAY_BASE(65536u);
    for (i = 0; i < 256; i++) nz += fb[i] != 0;
    ASSERT(nz > 0);
    ASSERT(tos_frame() == 600u);
    return 0;
}

static int t_life_golden(void) {
    int n, len, maxper, i; const uint8_t *fb; FILE *g; char hex[600]; int hn;
    ASSERT(fresh(1u) == 0);
    n = read_file("demos/life/life.c"); ASSERT(n > 0);
    len = tos_compile(TOS_LANG_C, g_src, (uint32_t)n, g_img, sizeof g_img, g_err, sizeof g_err);
    ASSERT(len > 0);
    ASSERT(tos_load_com(g_img, (uint32_t)len) == 0);
    ASSERT(run_frames(100, 0, 0, &maxper) == 100);
    ASSERT(maxper <= 500000);                         /* per-generation budget (SPEC WS6-03) */
    g = fopen("demos/life/gen100.expected", "rb"); ASSERT(g != NULL);
    hn = (int)fread(hex, 1u, 512u, g); fclose(g); ASSERT(hn == 512);
    fb = tos_tape_ptr(0) + TOS_DISPLAY_BASE(65536u);
    for (i = 0; i < 256; i++) {
        static const char hx[] = "0123456789abcdef";
        char b[2]; b[0] = hx[fb[i] >> 4]; b[1] = hx[fb[i] & 15u];
        ASSERT((b[0] == hex[2*i] || b[0] == (char)(hex[2*i] | 0x20)) && (b[1] == hex[2*i+1] || b[1] == (char)(hex[2*i+1] | 0x20)));
    }
    return 0;
}

static int t_bf_cells_on_tape1(void) {
    int n, len, i, nz1 = 0, nz0 = 0; char out[64]; int o = 0, ch;
    ASSERT(fresh(2u) == 0);
    n = read_file("demos/bf/hello.bf"); ASSERT(n > 0);
    len = bf_compile(g_src, (uint32_t)n, g_img, sizeof g_img, g_err, sizeof g_err); ASSERT(len > 0);
    ASSERT(tos_load_com(g_img, (uint32_t)len) == 0);
    for (i = 0; i < 2000 && tos_state() != KS_SHELL; i++) (void)tos_step(100000u);
    while ((ch = tos_con_pop()) >= 0 && o < 60) {
        out[o++] = (char)ch;
    }
    out[o] = 0;
    ASSERT(strncmp(out, "Hello World!\n", 13) == 0);
    for (i = 0; i < 16; i++) { nz1 += tos_tape_ptr(1)[0x4000 + i] != 0; nz0 += tos_tape_ptr(0)[0x4000 + i] != 0; }
    ASSERT(nz1 > 0);
    ASSERT(nz0 == 0);
    return 0;
}

static uint32_t travel_for(const char *tmpath, const char *word, uint8_t tapes) {
    FILE *f; char src[4096]; int n, len, i; char *p;
    f = fopen(tmpath, "rb"); if (f == NULL) return 0;
    n = (int)fread(src, 1u, sizeof src - 1u, f); fclose(f); src[n] = 0;
    /* replace the input: line (bounded copies; gcc's format-overflow analysis dislikes sprintf here) */
    p = strstr(src, "input:");
    if (p != NULL) {
        char *e = strchr(p, '\n');
        char tail[4096];
        size_t room = sizeof src - (size_t)(p - src);
        size_t wl = strlen(word);
        strncpy(tail, e ? e : "", sizeof tail - 1u); tail[sizeof tail - 1u] = 0;
        if (7u + wl + strlen(tail) + 1u > room) return 0;
        memcpy(p, "input: ", 7u); memcpy(p + 7, word, wl); strcpy(p + 7 + wl, tail);
    }
    if (fresh(tapes) != 0) return 0;
    len = tm_compile(src, (uint32_t)strlen(src), g_img, sizeof g_img, g_err, sizeof g_err); if (len <= 0) return 0;
    if (tos_load_com(g_img, (uint32_t)len) != 0) return 0;
    for (i = 0; i < 20000 && tos_state() != KS_SHELL; i++) (void)tos_step(100000u);
    return tos_travel_lo();
}

static int t_palindrome_travel(void) {
    const char *w32 = "abcabccbaabcabccbacbaabccbacbaab";   /* a 32-char palindrome? build one below */
    char pal[33]; int i; uint32_t t1, t2;
    for (i = 0; i < 16; i++) pal[i] = "abcabccbaabcabcc"[i];
    for (i = 0; i < 16; i++) pal[16 + i] = pal[15 - i];
    pal[32] = 0; (void)w32;
    t1 = travel_for("demos/tm/pal1.tm", pal, 2u);
    t2 = travel_for("demos/tm/pal2.tm", pal, 2u);
    ASSERT(t1 > 0 && t2 > 0);
    ASSERT(t1 > 2u * t2);
    return 0;
}

static uint8_t g_tape_a[65536];

static int pong_run_hash(uint8_t *tape_out, uint32_t *steps_out) {
    int n, len, i;
    if (fresh(1u) != 0) return -1;
    n = read_file("demos/pong/pong.c"); if (n <= 0) return -1;
    len = tos_compile(TOS_LANG_C, g_src, (uint32_t)n, g_img, sizeof g_img, g_err, sizeof g_err); if (len <= 0) return -1;
    if (tos_load_com(g_img, (uint32_t)len) != 0) return -1;
    tos_keys_set(TOS_KEY_W);
    for (i = 0; i < 100000 && tos_steps() < 100000u; i++) {
        (void)tos_step(1000u);
        if (tos_steps() >= 50000u) tos_keys_set(TOS_KEY_S);   /* same key log in both runs */
        if (tos_stop_reason() == KSTOP_HALT) break;
    }
    memcpy(tape_out, tos_tape_ptr(0), 65536u);
    *steps_out = tos_steps();
    return 0;
}

static int t_pong_deterministic(void) {
    uint32_t sa, sb;
    ASSERT(pong_run_hash(g_tape_a, &sa) == 0);
    {
        static uint8_t tape_b[65536];
        ASSERT(pong_run_hash(tape_b, &sb) == 0);
        ASSERT(sa == sb);
        ASSERT(sa >= 100000u);
        ASSERT(memcmp(g_tape_a, tape_b, 65536u) == 0);
    }
    return 0;
}

int main(void) {
    TEST("WS1-16 / WS4-05: two Pong runs with the same seed and key log are byte-identical at 100,000 steps", t_pong_deterministic);
    TEST("WS6-02: pong.c compiles <= 16128 bytes, runs 600 frames, <= 6000 steps/frame, draws", t_pong);
    TEST("WS6-03: life.c reaches the gen-100 golden framebuffer", t_life_golden);
    TEST("WS6-08: hello.bf keeps its cells on tape 1 when k=2 and prints Hello World!", t_bf_cells_on_tape1);
    TEST("WS6-05: single-tape palindrome head travel exceeds 2x the two-tape run at n=32", t_palindrome_travel);
    puts("PASS: test_v2_ws6_demos_api");
    RUN_ALL_TESTS();
}
