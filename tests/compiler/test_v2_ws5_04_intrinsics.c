/* WS5-04: tiny-C intrinsics (SPEC S3): peek/poke, peekw/pokew, __at placement, inp, bios, kbhit,
 * getchar/readline/lineget/linelen, tape/keys/vsync/ticks/rand. Runs through the tos_* API and checks
 * console bytes plus the tape bytes the intrinsics touched. */
#include "../testfw.h"
#include "api/api.h"
#include "compiler/compiler.h"
#include <stdint.h>
#include <string.h>

#define RUN_BUDGET 1000000u

static uint8_t  g_com[32768];
static uint8_t  g_com2[32768];
static char     g_err[512];
static char     g_out[16384];
static uint32_t g_out_len;
static uint32_t g_steps;

static int compile_c(const char *src)
{
    g_err[0] = 0;
    return cc_compile_buf(src, (uint32_t)strlen(src), g_com, (uint32_t)sizeof g_com, g_err, (uint32_t)sizeof g_err);
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

/* keys: key mask applied with tos_keys_set after tos_create (0 = leave untouched) */
static int run_prog(const tos_config_t *cfg, const uint8_t *com, uint32_t len, const char *input, uint8_t keys)
{
    uint32_t total = 0;
    int guard = 100000;
    if (tos_create(cfg) != 0) return -1;
    while (tos_con_pop() >= 0) { }
    if (input) { const char *p = input; while (*p) tos_con_push((uint8_t)*p++); }
    if (keys) tos_keys_set(keys);
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

#define PRINT_INT \
    "int print_int(int n) {\n" \
    "  int m;\n" \
    "  m = n;\n" \
    "  if (m < 0) { putchar('-'); m = 0 - m; }\n" \
    "  if (m >= 10) print_int(m / 10);\n" \
    "  putchar('0' + m % 10);\n" \
    "  return 0;\n" \
    "}\n"
#define NL "putchar('\\n');"

static int t_poke_peek_display(void)
{
    const uint8_t *t0;
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  poke(0xFE00, 0x80); print_int(peek(0xFE00)); " NL "\n"
        "  poke(0xFE01, 0x01); print_int(peek(0xFE01)); " NL "\n"
        "  poke(0x5000, 200); print_int(peek(0x5000)); " NL "\n"
        "  poke(0x5001, 300); print_int(peek(0x5001)); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(g_steps > 0);
    ASSERT(out_is("128\n1\n200\n44\n"));
    t0 = tos_tape_ptr(0);
    ASSERT(t0[TOS_DISPLAY_BASE(65536)] == 0x80);       /* top-left pixel (0,0) set */
    ASSERT(t0[TOS_DISPLAY_BASE(65536) + 1] == 0x01);
    ASSERT(t0[0x5000] == 200);
    ASSERT(t0[0x5001] == 44);
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

static int t_pokew_peekw(void)
{
    const uint8_t *t0;
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  pokew(0x5000, 0x1234); print_int(peekw(0x5000)); " NL "\n"
        "  print_int(peek(0x5000)); " NL "\n"
        "  print_int(peek(0x5001)); " NL "\n"
        "  poke(0x5010, 0x78); poke(0x5011, 0x56); print_int(peekw(0x5010)); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(out_is("4660\n52\n18\n22136\n"));
    t0 = tos_tape_ptr(0);
    ASSERT(t0[0x5000] == 0x34);
    ASSERT(t0[0x5001] == 0x12);
    return 0;
}

static int t_at_placement(void)
{
    const uint8_t *t0;
    int len, len_big, len_small;
    len = compile_c(PRINT_INT
        "__at(0xFE00) char vram[256];\n"
        "__at(0x5000) int w[2];\n"
        "__at(0x5100) int counter;\n"
        "int main() {\n"
        "  vram[0] = 0xC0; vram[255] = 3; vram[8] = vram[0];\n"
        "  w[1] = 0x0102; counter = 0x0403;\n"
        "  print_int(vram[255]); " NL "\n"
        "  print_int(w[1]); " NL "\n"
        "  print_int(peekw(0x5100)); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(out_is("3\n258\n1027\n"));
    t0 = tos_tape_ptr(0);
    ASSERT(t0[0xFE00] == 0xC0);
    ASSERT(t0[0xFEFF] == 0x03);
    ASSERT(t0[0xFE08] == 0xC0);
    ASSERT(t0[0x5002] == 0x02);
    ASSERT(t0[0x5003] == 0x01);
    ASSERT(t0[0x5100] == 0x03);
    ASSERT(t0[0x5101] == 0x04);

    /* __at globals occupy no image space: the array size must not change the output length */
    len_big = compile_c("__at(0x5000) char big[4000];\nint main() { big[0] = 7; return 0; }\n");
    ASSERT(len_big > 0);
    memcpy(g_com2, g_com, (size_t)len_big);
    len_small = compile_c("__at(0x5000) char big[1];\nint main() { big[0] = 7; return 0; }\n");
    ASSERT(len_small > 0);
    ASSERT(len_big == len_small);
    ASSERT(len_big < 1000);
    ASSERT(memcmp(g_com, g_com2, (size_t)len_small) == 0);
    return 0;
}

static int t_inp_tapes_and_pages(void)
{
    tos_config_t c;
    int len = compile_c(PRINT_INT
        "int main() { print_int(inp(4)); " NL " print_int(inp(5)); " NL " return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(out_is("1\n0\n"));
    c = cfg_with(2, 65536);
    ASSERT(run_prog(&c, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(out_is("2\n0\n"));
    c = cfg_with(4, 32768);
    ASSERT(run_prog(&c, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(out_is("4\n128\n"));
    c = cfg_with(1, 49152);
    ASSERT(run_prog(&c, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(out_is("1\n192\n"));
    return 0;
}

static int t_bios_const_and_kbhit(void)
{
    int len_no, len_yes;
    len_no = compile_c(PRINT_INT
        "int main() { print_int(bios(5, 0)); " NL " print_int(kbhit()); " NL " return 0; }\n");
    ASSERT(len_no > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len_no, NULL, 0) == 0);
    ASSERT(out_is("0\n0\n"));

    len_yes = compile_c(PRINT_INT
        "int main() { print_int(bios(5, 0)); " NL " print_int(kbhit()); " NL
        " putchar(getchar()); " NL " print_int(bios(5, 0)); " NL " return 0; }\n");
    ASSERT(len_yes > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len_yes, "z", 0) == 0);
    ASSERT(out_is("255\n255\nz\n0\n"));
    return 0;
}

static int t_getchar_readline(void)
{
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int c;\n"
        "  c = getchar(); putchar(c); " NL "\n"
        "  readline();\n"
        "  print_int(linelen()); " NL "\n"
        "  putchar(lineget(0)); putchar(lineget(2)); print_int(lineget(3)); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(NULL, g_com, (uint32_t)len, "qabc\n", 0) == 0);
    ASSERT(out_is("q\n3\nac0\n"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

static int t_tape_keys_vsync_rand(void)
{
    tos_config_t c = cfg_with(2, 65536);
    int len = compile_c(PRINT_INT
        "int main() {\n"
        "  int r;\n"
        "  tape(1); poke(0x5000, 0x55);\n"
        "  tape(0); poke(0x5000, 0x11);\n"
        "  print_int(inp(2)); " NL "\n"
        "  print_int(keys()); " NL "\n"
        "  print_int(inp(3)); " NL "\n"
        "  vsync(); vsync();\n"
        "  print_int(ticks()); " NL "\n"
        "  r = rand(); print_int(r >= 0 && r < 256); " NL "\n"
        "  return 0;\n"
        "}\n");
    ASSERT(len > 0);
    ASSERT(run_prog(&c, g_com, (uint32_t)len, NULL, (uint8_t)(TOS_KEY_W | TOS_KEY_SPACE)) == 0);
    ASSERT(out_is("0\n17\n17\n2\n1\n"));
    ASSERT(tos_tape_ptr(1)[0x5000] == 0x55);
    ASSERT(tos_tape_ptr(0)[0x5000] == 0x11);
    ASSERT(tos_frame() == 2);
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    return 0;
}

/* ---- failure cases ---- */

static int t_outp_bad_tape_halts(void)
{
    tos_config_t c = cfg_with(2, 65536);
    int len = compile_c("int main() { outp(2, 3); poke(0x5000, 1); putchar('X'); return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(&c, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(tos_halt_reason() == TOS_HALT_BAD_TAPE);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(tos_stop_reason() == KSTOP_HALT);
    ASSERT(strchr(g_out, 'X') == NULL);
    return 0;
}

static int t_poke_beyond_tape_faults(void)
{
    tos_config_t c = cfg_with(1, 32768);
    int len = compile_c("int main() { poke(0x9000, 1); putchar('X'); return 0; }\n");
    ASSERT(len > 0);
    ASSERT(run_prog(&c, g_com, (uint32_t)len, NULL, 0) == 0);
    ASSERT(tos_halt_reason() == TOS_HALT_TAPE_FAULT);
    ASSERT(tos_state() == KS_HALT);
    ASSERT(strchr(g_out, 'X') == NULL);
    return 0;
}

static int t_poke_undefined_argument(void)
{
    int len = compile_c("int main() { poke(0x5000, nope); return 0; }\n");
    ASSERT(len == -1);
    ASSERT(strncmp(g_err, "src.c:1:", 8) == 0);
    ASSERT(strstr(g_err, "undefined variable 'nope'") != NULL);
    return 0;
}

int main(void)
{
    TEST("WS5-04: poke(0xFE00,0x80) sets the top-left pixel; peek reads it back", t_poke_peek_display);
    TEST("WS5-04: pokew/peekw round-trip 0x1234 little-endian", t_pokew_peekw);
    TEST("WS5-04: __at(0xFE00) char vram[256] writes land at 0xFE00 and take no image space", t_at_placement);
    TEST("WS5-04: inp(4) is the tape count, inp(5) the page count", t_inp_tapes_and_pages);
    TEST("WS5-04: bios(5,0)/kbhit() are 0 before and 0xFF after console input is pushed", t_bios_const_and_kbhit);
    TEST("WS5-04: getchar/readline/linelen/lineget intrinsics", t_getchar_readline);
    TEST("WS5-04: tape()/keys()/vsync()/ticks()/rand() intrinsics", t_tape_keys_vsync_rand);
    TEST("WS5-04/WS1-07: outp(2,3) on 2 tapes halts with TOS_HALT_BAD_TAPE (failure)", t_outp_bad_tape_halts);
    TEST("WS5-04/WS4-02a: poke beyond a 32K tape halts with TOS_HALT_TAPE_FAULT (failure)", t_poke_beyond_tape_faults);
    TEST("WS5-04/WS5-08: undefined variable as intrinsic argument is a compile error (failure)", t_poke_undefined_argument);
    printf("PASS: test_v2_ws5_04_intrinsics\n");
    RUN_ALL_TESTS();
}
