/* TuringOS v2 — src/lang/tm.c
 * Turing-machine description language -> 8080 .com image (SPEC §S5; WS6-04, WS6-05, WS7-02).
 *
 * tm_compile() parses the description into static tables (<= 64 states, <= 4 tapes,
 * <= 64 symbols, <= 1024 rules) and emits a self-contained 8080 interpreter followed
 * by those tables.  The emitted program:
 *   - reads IN 4 (machine tape count k) and IN 5 (L/256).  TM tape j (0-based) lives
 *     on machine tape j mod k at bank offset (j div k)*8192 with its head at +4096;
 *     when (j div k)*8192 + 8192 exceeds the bank window it prints
 *     "line N: too many tapes\n" (N = the `tapes:` line) and HLTs;
 *   - writes the `input:` bytes on TM tape 0 from the head (those cells count as visited);
 *   - selects the machine tape with OUT 2 before every cell access; a cell holding
 *     byte 0 reads as the blank symbol;
 *   - tracks the min/max head position per TM tape; counts applied rules in a
 *     32-bit little-endian counter;
 *   - halts when the next state is `halt` or when no rule matches; in the latter
 *     case it prints the halting state's name + "\n" first (that is how the
 *     palindrome demos print yes/no: they halt in a state named `yes` or `no`);
 *   - then prints every TM tape from the leftmost to the rightmost visited cell,
 *     trimmed of leading and trailing blank symbols (an all-blank tape prints an
 *     empty line), each followed by "\n", then "steps=N\n" (decimal), then HLT.
 * Console output goes through BIOS CONOUT: MVI A,2 ; MOV C,byte ; OUT 1.
 * No <stdio.h>, no malloc; every table is static.  Errors: "line N: bad rule",
 * "line N: too many tapes", "line N: duplicate rule" (plus "too many states",
 * "too many symbols", "too many rules", "program too large").
 */
#include "tm.h"
#include "../tos.h"
#include <string.h>

#define TM_MAX_TAPES   4
#define TM_MAX_STATES  64
#define TM_MAX_SYMS    64
#define TM_MAX_RULES   1024
#define TM_NAME_MAX    63
#define TM_INPUT_MAX   4095
#define TM_MAX_TOK     8
#define TM_LBL_MAX     512
#define TM_FIX_MAX     1024
#define TM_ORG         0x0100u
#define TM_HALT_ID     0xFFu
#define TM_MV_S        0u
#define TM_MV_L        1u
#define TM_MV_R        2u

/* 8080 register codes used by the MOV/MVI/ALU encodings */
enum { RB = 0, RC = 1, RD = 2, RE = 3, RH = 4, RL = 5, RM = 6, RA = 7 };

/* single-byte opcodes */
#define OP_INX_D   0x13u
#define OP_INX_H   0x23u
#define OP_DCX_H   0x2Bu
#define OP_XCHG    0xEBu
#define OP_DAD_D   0x19u
#define OP_PUSH_B  0xC5u
#define OP_PUSH_D  0xD5u
#define OP_PUSH_H  0xE5u
#define OP_POP_B   0xC1u
#define OP_POP_D   0xD1u
#define OP_POP_H   0xE1u
#define OP_LDAX_D  0x1Au
#define OP_STAX_D  0x12u
#define OP_INR_C   0x0Cu
#define OP_DCR_B   0x05u
#define OP_RET     0xC9u
#define OP_RZ      0xC8u
#define OP_RC      0xD8u
#define OP_HLT     0x76u

/* ALU group bases: em_alu(base, reg) */
#define ALU_ADD 0x80u
#define ALU_SUB 0x90u
#define ALU_SBB 0x98u
#define ALU_ORA 0xB0u
#define ALU_CMP 0xB8u

/* per-TM-tape runtime record (8 bytes): HEAD u16, MIN u16, MAX u16, MT u8, pad */
#define TR_HEAD 0u
#define TR_MIN  2u
#define TR_MAX  4u
#define TR_MT   6u
#define TR_SIZE 8u

typedef struct {
    uint8_t state;
    uint8_t next;
    uint8_t rd[TM_MAX_TAPES];
    uint8_t wr[TM_MAX_TAPES];
    uint8_t mv[TM_MAX_TAPES];
} tm_rule_t;

typedef struct { const char *p; uint32_t n; } tm_tok_t;
typedef struct { uint16_t at; uint16_t lbl; uint16_t delta; } tm_fix_t;

static tm_rule_t g_rules[TM_MAX_RULES];
static int       g_nrules;
static char      g_names[TM_MAX_STATES][TM_NAME_MAX + 1];
static int       g_nstates;
static uint8_t   g_syms[TM_MAX_SYMS];
static int       g_nsyms;

static uint8_t   g_img[TOS_TPA_SIZE];
static uint32_t  g_pos;
static int       g_overflow;
static uint16_t  g_laddr[TM_LBL_MAX];
static int       g_nlabels;
static tm_fix_t  g_fix[TM_FIX_MAX];
static int       g_nfix;

/* ---- small helpers ----------------------------------------------------- */

static uint32_t dec_u32(char *dst, uint32_t v)
{
    char tmp[12];
    uint32_t n = 0, i;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
    for (i = 0; i < n; i++) dst[i] = tmp[n - 1u - i];
    return n;
}

static void set_err(char *err, uint32_t errcap, uint32_t line, const char *msg)
{
    char buf[96];
    uint32_t n, i;
    if (err == NULL || errcap == 0u) return;
    memcpy(buf, "line ", 5);
    n = 5u + dec_u32(buf + 5, line);
    buf[n++] = ':';
    buf[n++] = ' ';
    for (i = 0; msg[i] != 0 && n < (uint32_t)sizeof buf - 1u; i++) buf[n++] = msg[i];
    if (n > errcap - 1u) n = errcap - 1u;
    memcpy(err, buf, n);
    err[n] = 0;
}

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* a tape symbol: one printable, non-space ASCII char */
static int is_sym(unsigned char c)
{
    return c >= 0x21u && c <= 0x7Eu;
}

static int tokenize(const char *s, uint32_t n, tm_tok_t *t, int max)
{
    int cnt = 0;
    uint32_t i = 0;
    while (i < n) {
        uint32_t st;
        while (i < n && is_ws(s[i])) i++;
        if (i >= n) break;
        if (cnt >= max) return -1;
        st = i;
        while (i < n && !is_ws(s[i])) i++;
        t[cnt].p = s + st;
        t[cnt].n = i - st;
        cnt++;
    }
    return cnt;
}

static int tok_eq(const tm_tok_t *t, const char *lit)
{
    size_t l = strlen(lit);
    return t->n == (uint32_t)l && memcmp(t->p, lit, l) == 0;
}

/* returns id >= 0, -1 = too many states, -2 = bad name */
static int find_or_add_state(const char *p, uint32_t n)
{
    int i;
    if (n == 0u || n > TM_NAME_MAX) return -2;
    for (i = 0; i < g_nstates; i++) {
        if (strlen(g_names[i]) == (size_t)n && memcmp(g_names[i], p, (size_t)n) == 0) return i;
    }
    if (g_nstates >= TM_MAX_STATES) return -1;
    memcpy(g_names[g_nstates], p, (size_t)n);
    g_names[g_nstates][n] = 0;
    return g_nstates++;
}

static int add_sym(uint8_t c)
{
    int i;
    for (i = 0; i < g_nsyms; i++) if (g_syms[i] == c) return 0;
    if (g_nsyms >= TM_MAX_SYMS) return -1;
    g_syms[g_nsyms++] = c;
    return 0;
}

/* "(a,b,c)" with exactly nt items, or a single char when nt == 1 */
static int parse_syms(const tm_tok_t *t, int nt, uint8_t *out)
{
    uint32_t i;
    int j;
    if (t->n == 1u) {
        if (nt != 1 || !is_sym((unsigned char)t->p[0])) return -1;
        out[0] = (uint8_t)t->p[0];
        return 0;
    }
    if (t->p[0] != '(') return -1;
    i = 1;
    for (j = 0; j < nt; j++) {
        if (i >= t->n || !is_sym((unsigned char)t->p[i])) return -1;
        out[j] = (uint8_t)t->p[i];
        i++;
        if (j < nt - 1) {
            if (i >= t->n || t->p[i] != ',') return -1;
            i++;
        }
    }
    if (i >= t->n || t->p[i] != ')') return -1;
    i++;
    return i == t->n ? 0 : -1;
}

static int parse_moves(const tm_tok_t *t, int nt, uint8_t *out)
{
    uint8_t s[TM_MAX_TAPES];
    int j;
    if (parse_syms(t, nt, s) != 0) return -1;
    for (j = 0; j < nt; j++) {
        char c = (char)s[j];
        if (c == 'L' || c == 'l') out[j] = TM_MV_L;
        else if (c == 'R' || c == 'r') out[j] = TM_MV_R;
        else if (c == 'S' || c == 's') out[j] = TM_MV_S;
        else return -1;
    }
    return 0;
}

/* ---- 8080 emitter ------------------------------------------------------ */

static int newlabel(void)
{
    if (g_nlabels >= TM_LBL_MAX) { g_overflow = 1; return 0; }
    g_laddr[g_nlabels] = 0;
    return g_nlabels++;
}

static void here(int l)
{
    g_laddr[l] = (uint16_t)(TM_ORG + g_pos);
}

static void e8(uint32_t b)
{
    if (g_pos < (uint32_t)sizeof g_img) g_img[g_pos++] = (uint8_t)b;
    else g_overflow = 1;
}

static void e16(uint32_t w)
{
    e8(w & 0xFFu);
    e8((w >> 8) & 0xFFu);
}

static void eref(int l, uint32_t delta)
{
    if (g_nfix < TM_FIX_MAX) {
        g_fix[g_nfix].at = (uint16_t)g_pos;
        g_fix[g_nfix].lbl = (uint16_t)l;
        g_fix[g_nfix].delta = (uint16_t)delta;
        g_nfix++;
    } else {
        g_overflow = 1;
    }
    e16(0);
}

static void estr(const char *s)
{
    while (*s) e8((uint8_t)*s++);
}

static void em_mov(int d, int s)            { e8(0x40u | ((uint32_t)d << 3) | (uint32_t)s); }
static void em_mvi(int r, uint32_t v)       { e8(0x06u | ((uint32_t)r << 3)); e8(v); }
static void em_alu(uint32_t op, int r)      { e8(op | (uint32_t)r); }
static void em_lda(int l, uint32_t d)       { e8(0x3A); eref(l, d); }
static void em_sta(int l, uint32_t d)       { e8(0x32); eref(l, d); }
static void em_lhld(int l, uint32_t d)      { e8(0x2A); eref(l, d); }
static void em_shld(int l, uint32_t d)      { e8(0x22); eref(l, d); }
static void em_lxih_ref(int l, uint32_t d)  { e8(0x21); eref(l, d); }
static void em_lxih_imm(uint32_t v)         { e8(0x21); e16(v); }
static void em_lxid_ref(int l, uint32_t d)  { e8(0x11); eref(l, d); }
static void em_lxid_imm(uint32_t v)         { e8(0x11); e16(v); }
static void em_jmp(int l)                   { e8(0xC3); eref(l, 0); }
static void em_jz(int l)                    { e8(0xCA); eref(l, 0); }
static void em_jnz(int l)                   { e8(0xC2); eref(l, 0); }
static void em_jc(int l)                    { e8(0xDA); eref(l, 0); }
static void em_jnc(int l)                   { e8(0xD2); eref(l, 0); }
static void em_call(int l)                  { e8(0xCD); eref(l, 0); }
static void em_cpi(uint32_t v)              { e8(0xFE); e8(v); }
static void em_adi(uint32_t v)              { e8(0xC6); e8(v); }
static void em_in(uint32_t p)               { e8(0xDB); e8(p); }
static void em_out(uint32_t p)              { e8(0xD3); e8(p); }

/* OUT 2 with the machine tape of the TM tape record at lbl_tape (clobbers A) */
static void em_select(int lbl_tape)
{
    em_lda(lbl_tape, TR_MT);
    em_out(TOS_PORT_TAPE);
}

static int gen_image(int nt, uint8_t blank, uint32_t start_id, const uint8_t *input, uint32_t inlen,
                     uint32_t tapes_line)
{
    static const int ks[3] = { 4, 1, 2 };
    static const uint32_t pow10[10] = { 1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
                                        10000u, 1000u, 100u, 10u, 1u };
    int L_k1, L_k2, L_fit, L_go, L_step, L_scan, L_nextrec, L_nomatch, L_dump;
    int L_dg, L_dgs, L_dgd, L_sk, L_pr, L_toomany, L_putc, L_puts, L_sub32, L_dumptape;
    int L_f1, L_f1b, L_f1n, L_f1found, L_f2, L_f2b, L_f2found, L_f3, L_f3nb, L_f3n, L_nl;
    int L_cur, L_cursym, L_steps, L_tmp32, L_digbuf, L_pow10, L_dmt, L_dmin, L_dmax, L_dfirst, L_dlast;
    int L_stepstr, L_toomanystr, L_stp, L_namep, L_names, L_inputstr, L_rules, L_term;
    int L_tape[TM_MAX_TAPES];
    uint16_t first_off[TM_MAX_STATES], name_off[TM_MAX_STATES];
    uint8_t has_rules[TM_MAX_STATES];
    uint32_t recsz = 3u * (uint32_t)nt + 2u;
    uint32_t off, i;
    int j, s, r, ki;
    char numbuf[12];

    g_pos = 0; g_overflow = 0; g_nlabels = 0; g_nfix = 0;

    L_k1 = newlabel(); L_k2 = newlabel(); L_fit = newlabel(); L_go = newlabel();
    L_step = newlabel(); L_scan = newlabel(); L_nextrec = newlabel(); L_nomatch = newlabel();
    L_dump = newlabel(); L_dg = newlabel(); L_dgs = newlabel(); L_dgd = newlabel();
    L_sk = newlabel(); L_pr = newlabel(); L_toomany = newlabel();
    L_putc = newlabel(); L_puts = newlabel(); L_sub32 = newlabel(); L_dumptape = newlabel();
    L_f1 = newlabel(); L_f1b = newlabel(); L_f1n = newlabel(); L_f1found = newlabel();
    L_f2 = newlabel(); L_f2b = newlabel(); L_f2found = newlabel();
    L_f3 = newlabel(); L_f3nb = newlabel(); L_f3n = newlabel(); L_nl = newlabel();
    L_cur = newlabel(); L_cursym = newlabel(); L_steps = newlabel(); L_tmp32 = newlabel();
    L_digbuf = newlabel(); L_pow10 = newlabel(); L_dmt = newlabel(); L_dmin = newlabel();
    L_dmax = newlabel(); L_dfirst = newlabel(); L_dlast = newlabel();
    L_stepstr = newlabel(); L_toomanystr = newlabel(); L_stp = newlabel(); L_namep = newlabel();
    L_names = newlabel(); L_inputstr = newlabel(); L_rules = newlabel(); L_term = newlabel();
    for (j = 0; j < TM_MAX_TAPES; j++) L_tape[j] = newlabel();

    /* ---- start: place the TM tapes according to k = IN 4 ---- */
    em_in(TOS_PORT_TAPES);
    em_cpi(1); em_jz(L_k1);
    em_cpi(2); em_jz(L_k2);
    for (ki = 0; ki < 3; ki++) {
        int k = ks[ki];
        int qmax = (nt - 1) / k;
        if (k == 1) here(L_k1);
        else if (k == 2) here(L_k2);
        for (j = 0; j < nt; j++) {
            int q = j / k, mt = j % k;
            uint32_t head = TOS_BANK_BASE + (uint32_t)q * 0x2000u + 0x1000u;
            em_mvi(RA, (uint32_t)mt); em_sta(L_tape[j], TR_MT);
            em_lxih_imm(head);
            em_shld(L_tape[j], TR_HEAD); em_shld(L_tape[j], TR_MIN); em_shld(L_tape[j], TR_MAX);
        }
        /* pages needed: window 0x4000..L-0x2001 must hold (qmax+1) regions of 0x2000 */
        em_mvi(RA, 0x80u + (uint32_t)qmax * 0x20u);
        if (k != 2) em_jmp(L_fit);
    }
    here(L_fit);
    em_mov(RB, RA);
    em_in(TOS_PORT_PAGES);
    em_alu(ALU_ORA, RA); em_jz(L_go);          /* 0 = 64K: everything fits */
    em_alu(ALU_CMP, RB); em_jc(L_toomany);     /* pages < needed */
    here(L_go);

    /* ---- write `input:` on TM tape 0 from the head; those cells are visited ---- */
    if (inlen > 0u) {
        int L_inloop = newlabel();
        em_select(L_tape[0]);
        em_lhld(L_tape[0], TR_HEAD);
        em_lxid_ref(L_inputstr, 0);
        e8(0x01); e16(inlen);                      /* LXI B,inlen (16-bit count) */
        here(L_inloop);
        e8(OP_LDAX_D); em_mov(RM, RA); e8(OP_INX_D); e8(OP_INX_H);
        e8(0x0B);                                  /* DCX B */
        em_mov(RA, RB); em_alu(ALU_ORA, RC); em_jnz(L_inloop);
        e8(OP_DCX_H); em_shld(L_tape[0], TR_MAX);
    }

    /* ---- main step loop ---- */
    here(L_step);
    em_lda(L_cur, 0); em_cpi(TM_HALT_ID); em_jz(L_dump);
    for (j = 0; j < nt; j++) {
        int L_nb = newlabel();
        em_select(L_tape[j]);
        em_lhld(L_tape[j], TR_HEAD);
        em_mov(RA, RM); em_alu(ALU_ORA, RA); em_jnz(L_nb);
        em_mvi(RA, blank);
        here(L_nb);
        em_sta(L_cursym, (uint32_t)j);
    }
    /* HL = first rule record of the current state (STP[cur]) */
    em_lda(L_cur, 0); em_alu(ALU_ADD, RA); em_mov(RE, RA); em_mvi(RD, 0);
    em_lxih_ref(L_stp, 0); e8(OP_DAD_D); em_mov(RE, RM); e8(OP_INX_H); em_mov(RD, RM); e8(OP_XCHG);
    em_lda(L_cur, 0); em_mov(RB, RA);
    here(L_scan);
    em_mov(RA, RM); em_alu(ALU_CMP, RB); em_jnz(L_nomatch);   /* record.state != cur (0xFF terminator too) */
    e8(OP_PUSH_H); e8(OP_INX_H);
    for (j = 0; j < nt; j++) {
        em_lda(L_cursym, (uint32_t)j); em_alu(ALU_CMP, RM); em_jnz(L_nextrec); e8(OP_INX_H);
    }
    e8(OP_POP_D);                                              /* drop the saved record pointer */
    /* writes */
    for (j = 0; j < nt; j++) {
        em_mov(RC, RM); e8(OP_PUSH_H);
        em_select(L_tape[j]);
        em_lhld(L_tape[j], TR_HEAD); em_mov(RM, RC);
        e8(OP_POP_H); e8(OP_INX_H);
    }
    /* moves + min/max tracking */
    for (j = 0; j < nt; j++) {
        int L_a = newlabel(), L_b = newlabel(), L_c = newlabel(), L_d = newlabel();
        em_mov(RA, RM); e8(OP_PUSH_H);
        em_lhld(L_tape[j], TR_HEAD);
        em_cpi(TM_MV_L); em_jnz(L_a); e8(OP_DCX_H); em_jmp(L_b);
        here(L_a); em_cpi(TM_MV_R); em_jnz(L_b); e8(OP_INX_H);
        here(L_b); em_shld(L_tape[j], TR_HEAD);
        e8(OP_XCHG);                                           /* DE = head */
        em_lhld(L_tape[j], TR_MIN);
        em_mov(RA, RE); em_alu(ALU_SUB, RL); em_mov(RA, RD); em_alu(ALU_SBB, RH);  /* head - min */
        em_jnc(L_c);
        e8(OP_XCHG); em_shld(L_tape[j], TR_MIN); e8(OP_XCHG);
        here(L_c);
        em_lhld(L_tape[j], TR_MAX);
        em_mov(RA, RL); em_alu(ALU_SUB, RE); em_mov(RA, RH); em_alu(ALU_SBB, RD);  /* max - head */
        em_jnc(L_d);
        e8(OP_XCHG); em_shld(L_tape[j], TR_MAX);
        here(L_d);
        e8(OP_POP_H); e8(OP_INX_H);
    }
    /* next state */
    em_mov(RA, RM); em_sta(L_cur, 0);
    /* steps++ (32-bit little-endian) */
    em_lhld(L_steps, 0); e8(OP_INX_H); em_shld(L_steps, 0);
    em_mov(RA, RH); em_alu(ALU_ORA, RL); em_jnz(L_step);
    em_lhld(L_steps, 2); e8(OP_INX_H); em_shld(L_steps, 2);
    em_jmp(L_step);

    here(L_nextrec);
    e8(OP_POP_H); em_lxid_imm(recsz); e8(OP_DAD_D); em_jmp(L_scan);

    /* no rule matched. If the current state is a label state (no rules of its own, e.g. yes/no)
     * and it was entered by a transition (steps != 0), print its name + '\n'. States that have
     * rules point at an empty name, and a start state never left prints nothing (SPEC S5). */
    here(L_nomatch);
    em_lda(L_steps, 0); em_mov(RB, RA);
    em_lda(L_steps, 1); em_alu(ALU_ORA, RB); em_mov(RB, RA);
    em_lda(L_steps, 2); em_alu(ALU_ORA, RB); em_mov(RB, RA);
    em_lda(L_steps, 3); em_alu(ALU_ORA, RB); em_jz(L_dump);
    em_lda(L_cur, 0); em_alu(ALU_ADD, RA); em_mov(RE, RA); em_mvi(RD, 0);
    em_lxih_ref(L_namep, 0); e8(OP_DAD_D); em_mov(RE, RM); e8(OP_INX_H); em_mov(RD, RM); e8(OP_XCHG);
    em_mov(RA, RM); em_alu(ALU_ORA, RA); em_jz(L_dump);
    em_call(L_puts);
    em_mvi(RC, 10); em_call(L_putc);

    /* ---- dump every TM tape (trimmed), then steps=N, then HLT ---- */
    here(L_dump);
    for (j = 0; j < nt; j++) {
        em_lda(L_tape[j], TR_MT); em_sta(L_dmt, 0);
        em_lhld(L_tape[j], TR_MIN); em_shld(L_dmin, 0);
        em_lhld(L_tape[j], TR_MAX); em_shld(L_dmax, 0);
        em_call(L_dumptape);
    }
    em_lxih_ref(L_stepstr, 0); em_call(L_puts);
    /* 32-bit steps -> ten decimal digits by repeated subtraction of powers of ten */
    em_lxih_ref(L_pow10, 0); em_lxid_ref(L_digbuf, 0); em_mvi(RB, 10);
    here(L_dg);
    em_mvi(RC, 0);
    here(L_dgs);
    e8(OP_PUSH_H); e8(OP_PUSH_D); e8(OP_PUSH_B);
    em_call(L_sub32);
    e8(OP_POP_B); e8(OP_POP_D); e8(OP_POP_H);
    em_jc(L_dgd);
    e8(OP_INR_C); em_jmp(L_dgs);
    here(L_dgd);
    em_mov(RA, RC); e8(OP_STAX_D); e8(OP_INX_D);
    e8(OP_INX_H); e8(OP_INX_H); e8(OP_INX_H); e8(OP_INX_H);
    e8(OP_DCR_B); em_jnz(L_dg);
    /* print the digits without leading zeros (the last digit always prints) */
    em_lxih_ref(L_digbuf, 0); em_mvi(RB, 10);
    here(L_sk);
    em_mov(RA, RB); em_cpi(1); em_jz(L_pr);
    em_mov(RA, RM); em_alu(ALU_ORA, RA); em_jnz(L_pr);
    e8(OP_INX_H); e8(OP_DCR_B); em_jmp(L_sk);
    here(L_pr);
    em_mov(RA, RM); em_adi('0'); em_mov(RC, RA); em_call(L_putc);
    e8(OP_INX_H); e8(OP_DCR_B); em_jnz(L_pr);
    em_mvi(RC, 10); em_call(L_putc);
    e8(OP_HLT);

    here(L_toomany);
    em_lxih_ref(L_toomanystr, 0); em_call(L_puts);
    e8(OP_HLT);

    /* SUB32: HL -> P (u32 LE). If STEPS >= P then STEPS -= P and CY = 0, else CY = 1. */
    here(L_sub32);
    em_lxid_ref(L_steps, 0);
    e8(OP_LDAX_D); em_alu(ALU_SUB, RM); em_sta(L_tmp32, 0); e8(OP_INX_D); e8(OP_INX_H);
    e8(OP_LDAX_D); em_alu(ALU_SBB, RM); em_sta(L_tmp32, 1); e8(OP_INX_D); e8(OP_INX_H);
    e8(OP_LDAX_D); em_alu(ALU_SBB, RM); em_sta(L_tmp32, 2); e8(OP_INX_D); e8(OP_INX_H);
    e8(OP_LDAX_D); em_alu(ALU_SBB, RM); em_sta(L_tmp32, 3);
    e8(OP_RC);
    em_lhld(L_tmp32, 0); em_shld(L_steps, 0);
    em_lhld(L_tmp32, 2); em_shld(L_steps, 2);
    e8(OP_RET);

    /* DUMPTAPE: prints cells DMIN..DMAX of machine tape DMT trimmed of blanks, then '\n' */
    here(L_dumptape);
    em_lhld(L_dmin, 0);
    here(L_f1);                                    /* find the first non-blank */
    em_lda(L_dmt, 0); em_out(TOS_PORT_TAPE);
    em_mov(RA, RM); em_alu(ALU_ORA, RA); em_jz(L_f1b); em_cpi(blank); em_jnz(L_f1found);
    here(L_f1b);
    e8(OP_XCHG); em_lhld(L_dmax, 0);
    em_mov(RA, RL); em_alu(ALU_CMP, RE); em_jnz(L_f1n);
    em_mov(RA, RH); em_alu(ALU_CMP, RD); em_jz(L_nl);      /* all blank */
    here(L_f1n);
    e8(OP_XCHG); e8(OP_INX_H); em_jmp(L_f1);
    here(L_f1found);
    em_shld(L_dfirst, 0);
    em_lhld(L_dmax, 0);
    here(L_f2);                                    /* find the last non-blank (exists at >= first) */
    em_lda(L_dmt, 0); em_out(TOS_PORT_TAPE);
    em_mov(RA, RM); em_alu(ALU_ORA, RA); em_jz(L_f2b); em_cpi(blank); em_jnz(L_f2found);
    here(L_f2b);
    e8(OP_DCX_H); em_jmp(L_f2);
    here(L_f2found);
    em_shld(L_dlast, 0);
    em_lhld(L_dfirst, 0);
    here(L_f3);                                    /* print first..last */
    em_lda(L_dmt, 0); em_out(TOS_PORT_TAPE);
    em_mov(RA, RM); em_alu(ALU_ORA, RA); em_jnz(L_f3nb);
    em_mvi(RA, blank);
    here(L_f3nb);
    em_mov(RC, RA); em_call(L_putc);
    e8(OP_XCHG); em_lhld(L_dlast, 0);
    em_mov(RA, RL); em_alu(ALU_CMP, RE); em_jnz(L_f3n);
    em_mov(RA, RH); em_alu(ALU_CMP, RD); em_jz(L_nl);
    here(L_f3n);
    e8(OP_XCHG); e8(OP_INX_H); em_jmp(L_f3);
    here(L_nl);
    em_mvi(RC, 10); em_call(L_putc);
    e8(OP_RET);

    /* PUTS: HL -> NUL-terminated string */
    here(L_puts);
    em_mov(RA, RM); em_alu(ALU_ORA, RA); e8(OP_RZ);
    em_mov(RC, RA); em_call(L_putc); e8(OP_INX_H); em_jmp(L_puts);

    /* PUTC: C = byte; preserves BC, DE, HL */
    here(L_putc);
    e8(OP_PUSH_H); e8(OP_PUSH_D); e8(OP_PUSH_B);
    em_mvi(RA, TOS_BIOS_CONOUT); em_out(TOS_PORT_BIOS);
    e8(OP_POP_B); e8(OP_POP_D); e8(OP_POP_H);
    e8(OP_RET);

    /* ---- runtime data ---- */
    here(L_cur); e8(start_id);
    for (j = 0; j < nt; j++) {
        here(L_tape[j]);
        for (i = 0; i < TR_SIZE; i++) e8(0);
    }
    for (j = nt; j < TM_MAX_TAPES; j++) here(L_tape[j]);   /* unused records: harmless alias */
    here(L_cursym);
    for (j = 0; j < nt; j++) e8(0);
    here(L_steps);
    for (i = 0; i < 4u; i++) e8(0);
    here(L_tmp32);
    for (i = 0; i < 4u; i++) e8(0);
    here(L_digbuf);
    for (i = 0; i < 10u; i++) e8(0);
    here(L_pow10);
    for (i = 0; i < 10u; i++) {
        uint32_t v = pow10[i];
        e8(v & 0xFFu); e8((v >> 8) & 0xFFu); e8((v >> 16) & 0xFFu); e8((v >> 24) & 0xFFu);
    }
    here(L_dmt); e8(0);
    here(L_dmin); e16(0);
    here(L_dmax); e16(0);
    here(L_dfirst); e16(0);
    here(L_dlast); e16(0);
    here(L_stepstr); estr("steps="); e8(0);
    here(L_toomanystr);
    estr("line ");
    i = dec_u32(numbuf, tapes_line); numbuf[i] = 0; estr(numbuf);
    estr(": too many tapes\n"); e8(0);

    /* ---- tables: rules grouped by state, state -> first record, state -> name ---- */
    off = 0;
    for (s = 0; s < g_nstates; s++) {
        has_rules[s] = 0; first_off[s] = 0;
        for (r = 0; r < g_nrules; r++) {
            if ((int)g_rules[r].state != s) continue;
            if (!has_rules[s]) { has_rules[s] = 1; first_off[s] = (uint16_t)off; }
            off += recsz;
        }
    }
    off = 1;   /* offset 0 of the name blob is the empty string used by states that have rules */
    for (s = 0; s < g_nstates; s++) {
        name_off[s] = (uint16_t)off;
        off += (uint32_t)strlen(g_names[s]) + 1u;
    }
    here(L_stp);
    for (s = 0; s < g_nstates; s++) {
        if (has_rules[s]) eref(L_rules, first_off[s]);
        else eref(L_term, 0);
    }
    here(L_namep);
    for (s = 0; s < g_nstates; s++) eref(L_names, has_rules[s] ? 0u : name_off[s]);
    here(L_names);
    e8(0);
    for (s = 0; s < g_nstates; s++) { estr(g_names[s]); e8(0); }
    here(L_inputstr);
    for (i = 0; i < inlen; i++) e8(input[i]);
    here(L_rules);
    for (s = 0; s < g_nstates; s++) {
        for (r = 0; r < g_nrules; r++) {
            const tm_rule_t *R = &g_rules[r];
            if ((int)R->state != s) continue;
            e8(R->state);
            for (j = 0; j < nt; j++) e8(R->rd[j]);
            for (j = 0; j < nt; j++) e8(R->wr[j]);
            for (j = 0; j < nt; j++) e8(R->mv[j]);
            e8(R->next);
        }
    }
    here(L_term); e8(0xFFu);

    /* ---- resolve fixups ---- */
    for (r = 0; r < g_nfix; r++) {
        uint32_t v = (uint32_t)g_laddr[g_fix[r].lbl] + (uint32_t)g_fix[r].delta;
        uint32_t at = g_fix[r].at;
        if (at + 1u < (uint32_t)sizeof g_img) {
            g_img[at] = (uint8_t)(v & 0xFFu);
            g_img[at + 1u] = (uint8_t)((v >> 8) & 0xFFu);
        }
    }
    return g_overflow ? -1 : 0;
}

/* ---- parser + driver --------------------------------------------------- */

int tm_compile(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap)
{
    int nt = 1;
    uint8_t blank = '_';
    int have_start = 0;
    char start_name[TM_NAME_MAX + 1];
    uint32_t start_line = 1;
    uint32_t start_id = 0;
    uint8_t input[TM_INPUT_MAX];
    uint32_t inlen = 0;
    uint32_t tapes_line = 1;
    uint32_t line = 0, pos = 0;

    g_nrules = 0; g_nstates = 0; g_nsyms = 0;
    start_name[0] = 0;
    if (err != NULL && errcap != 0u) err[0] = 0;
    if (src == NULL) len = 0;

    while (pos < len) {
        uint32_t ls = pos, le, e, b;
        tm_tok_t t[TM_MAX_TOK];
        int ntok, i, j, id;
        tm_rule_t *R;

        while (pos < len && src[pos] != '\n') pos++;
        le = pos;
        if (pos < len) pos++;
        line++;

        /* strip the comment, then leading/trailing whitespace */
        e = ls;
        while (e < le && src[e] != '#') e++;
        b = ls;
        while (b < e && is_ws(src[b])) b++;
        while (e > b && is_ws(src[e - 1u])) e--;
        if (b == e) continue;

        /* directives: tapes: blank: start: input: */
        if (e - b >= 6u && (memcmp(src + b, "tapes:", 6) == 0 || memcmp(src + b, "blank:", 6) == 0 ||
                            memcmp(src + b, "start:", 6) == 0 || memcmp(src + b, "input:", 6) == 0)) {
            const char *key = src + b;
            uint32_t vb = b + 6u, vn, k;
            while (vb < e && is_ws(src[vb])) vb++;
            vn = e - vb;
            if (memcmp(key, "tapes:", 6) == 0) {
                uint32_t v = 0;
                if (vn == 0u || vn > 3u || g_nrules > 0) { set_err(err, errcap, line, "bad rule"); return -1; }
                for (k = 0; k < vn; k++) {
                    char c = src[vb + k];
                    if (c < '0' || c > '9') { set_err(err, errcap, line, "bad rule"); return -1; }
                    v = v * 10u + (uint32_t)(c - '0');
                }
                if (v > TM_MAX_TAPES) { set_err(err, errcap, line, "too many tapes"); return -1; }
                if (v == 0u) { set_err(err, errcap, line, "bad rule"); return -1; }
                nt = (int)v;
                tapes_line = line;
            } else if (memcmp(key, "blank:", 6) == 0) {
                if (vn != 1u || !is_sym((unsigned char)src[vb])) { set_err(err, errcap, line, "bad rule"); return -1; }
                blank = (uint8_t)src[vb];
            } else if (memcmp(key, "start:", 6) == 0) {
                if (vn == 0u || vn > TM_NAME_MAX) { set_err(err, errcap, line, "bad rule"); return -1; }
                for (k = 0; k < vn; k++) {
                    if (is_ws(src[vb + k])) { set_err(err, errcap, line, "bad rule"); return -1; }
                }
                memcpy(start_name, src + vb, vn);
                start_name[vn] = 0;
                have_start = 1;
                start_line = line;
            } else {
                if (vn > TM_INPUT_MAX) { set_err(err, errcap, line, "bad rule"); return -1; }
                for (k = 0; k < vn; k++) {
                    unsigned char c = (unsigned char)src[vb + k];
                    if (c < 0x20u || c > 0x7Eu) { set_err(err, errcap, line, "bad rule"); return -1; }
                    input[k] = (uint8_t)c;
                }
                inlen = vn;
            }
            continue;
        }

        /* rule: state read -> write move next */
        ntok = tokenize(src + b, e - b, t, TM_MAX_TOK);
        if (ntok != 6 || !tok_eq(&t[2], "->")) { set_err(err, errcap, line, "bad rule"); return -1; }
        if (tok_eq(&t[0], "halt")) { set_err(err, errcap, line, "bad rule"); return -1; }
        if (g_nrules >= TM_MAX_RULES) { set_err(err, errcap, line, "too many rules"); return -1; }
        R = &g_rules[g_nrules];
        memset(R, 0, sizeof *R);
        if (parse_syms(&t[1], nt, R->rd) != 0 || parse_syms(&t[3], nt, R->wr) != 0 ||
            parse_moves(&t[4], nt, R->mv) != 0) {
            set_err(err, errcap, line, "bad rule"); return -1;
        }
        for (j = 0; j < nt; j++) {
            if (add_sym(R->rd[j]) != 0 || add_sym(R->wr[j]) != 0) {
                set_err(err, errcap, line, "too many symbols"); return -1;
            }
        }
        id = find_or_add_state(t[0].p, t[0].n);
        if (id == -2) { set_err(err, errcap, line, "bad rule"); return -1; }
        if (id == -1) { set_err(err, errcap, line, "too many states"); return -1; }
        R->state = (uint8_t)id;
        if (tok_eq(&t[5], "halt")) {
            R->next = (uint8_t)TM_HALT_ID;
        } else {
            id = find_or_add_state(t[5].p, t[5].n);
            if (id == -2) { set_err(err, errcap, line, "bad rule"); return -1; }
            if (id == -1) { set_err(err, errcap, line, "too many states"); return -1; }
            R->next = (uint8_t)id;
        }
        for (i = 0; i < g_nrules; i++) {
            if (g_rules[i].state == R->state && memcmp(g_rules[i].rd, R->rd, (size_t)nt) == 0) {
                set_err(err, errcap, line, "duplicate rule"); return -1;
            }
        }
        g_nrules++;
    }

    if (have_start) {
        if (strcmp(start_name, "halt") == 0) {
            start_id = TM_HALT_ID;
        } else {
            int id = find_or_add_state(start_name, (uint32_t)strlen(start_name));
            if (id == -2) { set_err(err, errcap, start_line, "bad rule"); return -1; }
            if (id == -1) { set_err(err, errcap, start_line, "too many states"); return -1; }
            start_id = (uint32_t)id;
        }
    } else if (g_nrules > 0) {
        start_id = g_rules[0].state;
    } else {
        set_err(err, errcap, line != 0u ? line : 1u, "bad rule");
        return -1;
    }

    if (gen_image(nt, blank, start_id, input, inlen, tapes_line) != 0 || out == NULL || g_pos > cap) {
        set_err(err, errcap, line != 0u ? line : 1u, "program too large");
        return -1;
    }
    memcpy(out, g_img, g_pos);
    return (int)g_pos;
}
