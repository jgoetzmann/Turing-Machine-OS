/* src/lang/bf.c — Brainfuck -> Intel 8080 .com compiler (TuringOS v2, SPEC S6).
 *
 * Image layout (ORG 0100H; the loader sets SP, so no LXI SP is emitted):
 *
 *   0100  prologue  IN 04H ; CPI 2 ; JC skip ; MVI A,1 ; OUT 02H       cells on tape 1 when k >= 2
 *         skip:     IN 05H ; SUI 60H ; MOV H,A ; MVI L,0              HL = window size = L - 6000H
 *                   CPI 76H ; JC nocap ; LXI H,7530H                  cap the cell count at 30000
 *         nocap:    SHLD count ; XCHG ; LXI H,4000H ; DAD D ; SHLD limit
 *                   XRA A ; SUB E ; MOV L,A ; MVI A,0 ; SBB D ; MOV H,A ; SHLD negcount
 *                   LXI H,4000H ; JMP main                             HL = cell 0
 *   0135  runtime   right1: LXI D,1     right: HL += DE ; if HL >= limit then HL -= count ; RET
 *   0149            left1:  LXI D,-1    left:  HL += DE ; if HL <  4000H then HL += count ; RET
 *   0157  vars      count: DW 0   limit: DW 0   negcount: DW 0
 *   015D  body      HL is the cell pointer throughout.
 *                   '+'/'-' run  -> INR M | DCR M | MOV A,M ; ADI n ; MOV M,A
 *                   '>'/'<' run  -> CALL right1 | CALL left1 | LXI D,n ; CALL right|left
 *                   '['          -> MOV A,M ; ORA A ; JZ end           (end patched at the matching ']')
 *                   ']'          -> JMP start                          (start = the '[' test)
 *                   ','          -> MVI A,1 ; OUT 01H ; MOV M,A        BIOS CONIN
 *                   '.'          -> MOV C,M ; MVI A,2 ; OUT 01H        BIOS CONOUT
 *         epilogue  HLT
 *
 * Cells are bytes in the banked window from 4000H on tape 1 (tape 0 on a 1-tape machine);
 * cell count = min(30000, window size) = 8192 @32K, 24576 @48K, 30000 @64K; the pointer wraps
 * inside [0, count). Cells are never cleared. Any character that is not one of the eight
 * commands is ignored; '\n' counts lines (1-based); a NUL byte ends the source.
 *
 * Errors (err = "line N: message", -1 returned):
 *   "unmatched '['"       line of the outermost '[' still open at end of input
 *   "unmatched ']'"       line of the offending ']'
 *   "program too large"   output would exceed min(cap, TOS_TPA_SIZE) or nesting exceeds 4096
 * Bracket errors are reported before any code is generated, so they never depend on cap.
 */
#include "bf.h"
#include "../tos.h"
#include <stdint.h>

#define BF_ORG         0x0100u
#define BF_CELL_BASE   0x4000u
#define BF_MAX_CELLS   30000u
#define BF_MOVE_CHUNK  4096            /* largest pointer move per CALL; smaller than any cell count */
#define BF_DEPTH_MAX   4096u

/* ---- output buffer ------------------------------------------------------ */

typedef struct {
    uint8_t  *out;
    uint32_t  cap;
    uint32_t  len;
    int       overflow;
} bf_emit_t;

static void emit8(bf_emit_t *e, uint8_t b)
{
    if (e->out == 0 || e->len >= e->cap) {
        e->overflow = 1;
        return;
    }
    e->out[e->len++] = b;
}

static void emit16(bf_emit_t *e, uint16_t v)
{
    emit8(e, (uint8_t)(v & 0xFFu));
    emit8(e, (uint8_t)((v >> 8) & 0xFFu));
}

static void patch16(bf_emit_t *e, uint32_t off, uint16_t v)
{
    if (e->out != 0 && off + 1u < e->cap) {
        e->out[off]      = (uint8_t)(v & 0xFFu);
        e->out[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    }
}

/* ---- error text ---------------------------------------------------------- */

static uint32_t put_str(char *dst, uint32_t cap, uint32_t pos, const char *s)
{
    while (*s) {
        if (pos + 1u < cap) dst[pos] = *s;
        pos++;
        s++;
    }
    return pos;
}

static uint32_t put_uint(char *dst, uint32_t cap, uint32_t pos, uint32_t v)
{
    char tmp[12];
    int  n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v);
    while (n > 0) {
        n--;
        if (pos + 1u < cap) dst[pos] = tmp[n];
        pos++;
    }
    return pos;
}

static int bf_fail(char *err, uint32_t errcap, uint32_t line, const char *msg)
{
    if (err && errcap) {
        uint32_t pos = 0;
        pos = put_str(err, errcap, pos, "line ");
        pos = put_uint(err, errcap, pos, line);
        pos = put_str(err, errcap, pos, ": ");
        pos = put_str(err, errcap, pos, msg);
        if (pos >= errcap) pos = errcap - 1u;
        err[pos] = 0;
    }
    return -1;
}

/* ---- source scanner ------------------------------------------------------ */

typedef struct {
    const char *src;
    uint32_t    len;
    uint32_t    pos;
    uint32_t    line;
} bf_scan_t;

static int is_cmd(char c)
{
    return c == '+' || c == '-' || c == '<' || c == '>' ||
           c == '[' || c == ']' || c == '.' || c == ',';
}

/* Next command character, or 0 at end of input (a NUL byte also ends the input).
 * *line receives the 1-based line of the command returned. Everything else is skipped. */
static char scan_next(bf_scan_t *s, uint32_t *line)
{
    while (s->pos < s->len) {
        char c = s->src[s->pos++];
        if (c == 0) {
            s->pos = s->len;
            return 0;
        }
        if (c == '\n') {
            s->line++;
            continue;
        }
        if (is_cmd(c)) {
            *line = s->line;
            return c;
        }
    }
    return 0;
}

static char scan_peek(const bf_scan_t *s)
{
    bf_scan_t t = *s;
    uint32_t  l = 0;
    return scan_next(&t, &l);
}

/* ---- bracket stack ------------------------------------------------------- */

typedef struct {
    uint32_t patch_off;   /* image offset of the JZ operand */
    uint16_t start_addr;  /* address of the '[' test (MOV A,M) */
    uint32_t line;        /* source line of the '[' */
} bf_frame_t;

static bf_frame_t bf_stack[BF_DEPTH_MAX];

/* Pass 1: bracket matching only. Returns 0 when balanced, else -1 with err filled. */
static int bf_check_brackets(const char *src, uint32_t len, char *err, uint32_t errcap)
{
    bf_scan_t sc;
    uint32_t  depth = 0;
    uint32_t  line = 1;
    char      c;

    sc.src = src;
    sc.len = len;
    sc.pos = 0;
    sc.line = 1;

    while ((c = scan_next(&sc, &line)) != 0) {
        if (c == '[') {
            if (depth >= BF_DEPTH_MAX) return bf_fail(err, errcap, line, "program too large");
            bf_stack[depth].line = line;
            depth++;
        } else if (c == ']') {
            if (depth == 0) return bf_fail(err, errcap, line, "unmatched ']'");
            depth--;
        }
    }
    if (depth > 0) return bf_fail(err, errcap, bf_stack[0].line, "unmatched '['");
    return 0;
}

/* ---- code generators ----------------------------------------------------- */

static void emit_call(bf_emit_t *e, uint16_t addr)
{
    emit8(e, 0xCD);                     /* CALL addr */
    emit16(e, addr);
}

static void emit_lxi_d(bf_emit_t *e, uint16_t v)
{
    emit8(e, 0x11);                     /* LXI D,v */
    emit16(e, v);
}

/* A run of '+'/'-' with net delta (any sign; taken modulo 256). */
static void emit_add(bf_emit_t *e, int delta)
{
    uint8_t v = (uint8_t)((uint32_t)delta & 0xFFu);
    if (v == 0) return;
    if (v == 1) {
        emit8(e, 0x34);                 /* INR M */
    } else if (v == 0xFF) {
        emit8(e, 0x35);                 /* DCR M */
    } else {
        emit8(e, 0x7E);                 /* MOV A,M */
        emit8(e, 0xC6);                 /* ADI v */
        emit8(e, v);
        emit8(e, 0x77);                 /* MOV M,A */
    }
}

typedef struct {
    uint16_t right1, right, left1, left;
} bf_runtime_t;

/* A run of '>'/'<' with net delta (any sign). Moves are chunked so that one wrap check
 * per CALL is enough (|chunk| <= 4096 < smallest cell count 8192). */
static void emit_move(bf_emit_t *e, int delta, const bf_runtime_t *rt)
{
    while (delta > BF_MOVE_CHUNK) {
        emit_lxi_d(e, (uint16_t)BF_MOVE_CHUNK);
        emit_call(e, rt->right);
        delta -= BF_MOVE_CHUNK;
    }
    while (delta < -BF_MOVE_CHUNK) {
        emit_lxi_d(e, (uint16_t)(65536 - BF_MOVE_CHUNK));
        emit_call(e, rt->left);
        delta += BF_MOVE_CHUNK;
    }
    if (delta == 1) {
        emit_call(e, rt->right1);
    } else if (delta == -1) {
        emit_call(e, rt->left1);
    } else if (delta > 1) {
        emit_lxi_d(e, (uint16_t)delta);
        emit_call(e, rt->right);
    } else if (delta < -1) {
        emit_lxi_d(e, (uint16_t)(65536 + delta));
        emit_call(e, rt->left);
    }
}

/* ---- prologue + runtime -------------------------------------------------- */

enum { L_SKIP = 0, L_NOCAP, L_MAIN, L_COUNT, L_LIMIT, L_NEG, L_CLR, L_LABELS };

typedef struct {
    uint32_t off;
    int      label;
} bf_fixup_t;

static void emit_prologue(bf_emit_t *e, bf_runtime_t *rt)
{
    uint16_t   labels[L_LABELS];
    bf_fixup_t fix[24];
    int        nfix = 0;
    int        i;

#define BF_REF(lbl)  do { fix[nfix].off = e->len; fix[nfix].label = (lbl); nfix++; emit16(e, 0); } while (0)
#define BF_HERE(lbl) (labels[lbl] = (uint16_t)(BF_ORG + e->len))

    for (i = 0; i < L_LABELS; i++) labels[i] = 0;

    /* ---- tape select: k >= 2 -> cells on tape 1 ---- */
    emit8(e, 0xDB); emit8(e, (uint8_t)TOS_PORT_TAPES);   /* IN 04H          A = k */
    emit8(e, 0xFE); emit8(e, 0x02);                      /* CPI 2           CY when k < 2 */
    emit8(e, 0xDA); BF_REF(L_SKIP);                      /* JC skip */
    emit8(e, 0x3E); emit8(e, 0x01);                      /* MVI A,1 */
    emit8(e, 0xD3); emit8(e, (uint8_t)TOS_PORT_TAPE);    /* OUT 02H         select tape 1 */
    BF_HERE(L_SKIP);

    /* ---- cell count = min(30000, L - 6000H) ---- */
    emit8(e, 0xDB); emit8(e, (uint8_t)TOS_PORT_PAGES);   /* IN 05H          A = (L/256) & 0xFF */
    emit8(e, 0xD6); emit8(e, 0x60);                      /* SUI 60H         A = window >> 8 (wraps for 64K: 0xA0) */
    emit8(e, 0x67);                                      /* MOV H,A */
    emit8(e, 0x2E); emit8(e, 0x00);                      /* MVI L,0         HL = window size */
    emit8(e, 0xFE); emit8(e, 0x76);                      /* CPI 76H         CY when HL <= 7500H (< 30000) */
    emit8(e, 0xDA); BF_REF(L_NOCAP);                     /* JC nocap */
    emit8(e, 0x21); emit16(e, (uint16_t)BF_MAX_CELLS);   /* LXI H,7530H     HL = 30000 */
    BF_HERE(L_NOCAP);
    emit8(e, 0x22); BF_REF(L_COUNT);                     /* SHLD count */
    emit8(e, 0xEB);                                      /* XCHG            DE = count */
    emit8(e, 0x21); emit16(e, (uint16_t)BF_CELL_BASE);   /* LXI H,4000H */
    emit8(e, 0x19);                                      /* DAD D           HL = 4000H + count */
    emit8(e, 0x22); BF_REF(L_LIMIT);                     /* SHLD limit */
    emit8(e, 0xAF);                                      /* XRA A           A = 0, CY = 0 */
    emit8(e, 0x93);                                      /* SUB E */
    emit8(e, 0x6F);                                      /* MOV L,A */
    emit8(e, 0x3E); emit8(e, 0x00);                      /* MVI A,0         flags untouched */
    emit8(e, 0x9A);                                      /* SBB D */
    emit8(e, 0x67);                                      /* MOV H,A         HL = -count */
    emit8(e, 0x22); BF_REF(L_NEG);                       /* SHLD negcount */
    /* ---- clear every cell so a program never inherits a previous run's tape ---- */
    emit8(e, 0x2A); BF_REF(L_COUNT);                     /* LHLD count */
    emit8(e, 0x44); emit8(e, 0x4D);                      /* MOV B,H ; MOV C,L   BC = count */
    emit8(e, 0x21); emit16(e, (uint16_t)BF_CELL_BASE);   /* LXI H,4000H */
    BF_HERE(L_CLR);
    emit8(e, 0x36); emit8(e, 0x00);                      /* MVI M,0 */
    emit8(e, 0x23);                                      /* INX H */
    emit8(e, 0x0B);                                      /* DCX B */
    emit8(e, 0x78); emit8(e, 0xB1);                      /* MOV A,B ; ORA C */
    emit8(e, 0xC2); BF_REF(L_CLR);                       /* JNZ clr */
    emit8(e, 0x21); emit16(e, (uint16_t)BF_CELL_BASE);   /* LXI H,4000H     HL = cell 0 */
    emit8(e, 0xC3); BF_REF(L_MAIN);                      /* JMP main */

    /* ---- right1: DE = 1 ; right: HL += DE, wrap when HL >= limit ---- */
    rt->right1 = (uint16_t)(BF_ORG + e->len);
    emit8(e, 0x11); emit16(e, 0x0001);                   /* LXI D,1 */
    rt->right = (uint16_t)(BF_ORG + e->len);
    emit8(e, 0x19);                                      /* DAD D           HL = ptr + n */
    emit8(e, 0xEB);                                      /* XCHG            DE = ptr' */
    emit8(e, 0x2A); BF_REF(L_LIMIT);                     /* LHLD limit */
    emit8(e, 0x7B);                                      /* MOV A,E */
    emit8(e, 0x95);                                      /* SUB L */
    emit8(e, 0x7A);                                      /* MOV A,D */
    emit8(e, 0x9C);                                      /* SBB H           CY = ptr' < limit */
    emit8(e, 0xEB);                                      /* XCHG            HL = ptr' (flags kept) */
    emit8(e, 0xD8);                                      /* RC              still inside the tape */
    emit8(e, 0xEB);                                      /* XCHG            DE = ptr' */
    emit8(e, 0x2A); BF_REF(L_NEG);                       /* LHLD negcount */
    emit8(e, 0x19);                                      /* DAD D           HL = ptr' - count */
    emit8(e, 0xC9);                                      /* RET */

    /* ---- left1: DE = -1 ; left: HL += DE, wrap when HL < 4000H ---- */
    rt->left1 = (uint16_t)(BF_ORG + e->len);
    emit8(e, 0x11); emit16(e, 0xFFFF);                   /* LXI D,0FFFFH */
    rt->left = (uint16_t)(BF_ORG + e->len);
    emit8(e, 0x19);                                      /* DAD D           HL = ptr - n (>= 3000H) */
    emit8(e, 0x7C);                                      /* MOV A,H */
    emit8(e, 0xFE); emit8(e, 0x40);                      /* CPI 40H         CY when H < 40H */
    emit8(e, 0xD0);                                      /* RNC             ptr' >= 4000H */
    emit8(e, 0xEB);                                      /* XCHG            DE = ptr' */
    emit8(e, 0x2A); BF_REF(L_COUNT);                     /* LHLD count */
    emit8(e, 0x19);                                      /* DAD D           HL = ptr' + count */
    emit8(e, 0xC9);                                      /* RET */

    /* ---- runtime variables ---- */
    BF_HERE(L_COUNT); emit16(e, 0);                      /* count:    DW 0 */
    BF_HERE(L_LIMIT); emit16(e, 0);                      /* limit:    DW 0 */
    BF_HERE(L_NEG);   emit16(e, 0);                      /* negcount: DW 0 */

    BF_HERE(L_MAIN);

    for (i = 0; i < nfix; i++) patch16(e, fix[i].off, labels[fix[i].label]);

#undef BF_REF
#undef BF_HERE
}

/* ---- entry point ---------------------------------------------------------- */

int bf_compile(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap)
{
    bf_emit_t    e;
    bf_scan_t    sc;
    bf_runtime_t rt;
    uint32_t     depth = 0;
    uint32_t     line = 1;
    char         c;

    if (err && errcap) err[0] = 0;
    if (src == 0) len = 0;

    /* pass 1: bracket structure (independent of the output buffer) */
    if (bf_check_brackets(src, len, err, errcap) < 0) return -1;

    /* pass 2: code generation */
    e.out = out;
    e.cap = cap > TOS_TPA_SIZE ? TOS_TPA_SIZE : cap;
    e.len = 0;
    e.overflow = 0;

    sc.src = src;
    sc.len = len;
    sc.pos = 0;
    sc.line = 1;

    emit_prologue(&e, &rt);
    if (e.overflow) return bf_fail(err, errcap, line, "program too large");

    while ((c = scan_next(&sc, &line)) != 0) {
        switch (c) {
        case '+':
        case '-': {
            int delta = (c == '+') ? 1 : -1;
            for (;;) {
                char     p = scan_peek(&sc);
                uint32_t l2 = 0;
                if (p != '+' && p != '-') break;
                scan_next(&sc, &l2);
                delta += (p == '+') ? 1 : -1;
            }
            emit_add(&e, delta);
            break;
        }
        case '>':
        case '<': {
            int delta = (c == '>') ? 1 : -1;
            for (;;) {
                char     p = scan_peek(&sc);
                uint32_t l2 = 0;
                if (p != '>' && p != '<') break;
                scan_next(&sc, &l2);
                delta += (p == '>') ? 1 : -1;
            }
            emit_move(&e, delta, &rt);
            break;
        }
        case '.':
            emit8(&e, 0x4E);                                    /* MOV C,M */
            emit8(&e, 0x3E); emit8(&e, (uint8_t)TOS_BIOS_CONOUT); /* MVI A,2 */
            emit8(&e, 0xD3); emit8(&e, (uint8_t)TOS_PORT_BIOS);   /* OUT 01H */
            break;
        case ',':
            emit8(&e, 0x3E); emit8(&e, (uint8_t)TOS_BIOS_CONIN);  /* MVI A,1 */
            emit8(&e, 0xD3); emit8(&e, (uint8_t)TOS_PORT_BIOS);   /* OUT 01H         A = byte */
            emit8(&e, 0x77);                                    /* MOV M,A */
            break;
        case '[':
            if (depth >= BF_DEPTH_MAX) return bf_fail(err, errcap, line, "program too large");
            bf_stack[depth].start_addr = (uint16_t)(BF_ORG + e.len);
            bf_stack[depth].line = line;
            emit8(&e, 0x7E);                                    /* MOV A,M */
            emit8(&e, 0xB7);                                    /* ORA A */
            emit8(&e, 0xCA);                                    /* JZ end */
            bf_stack[depth].patch_off = e.len;
            emit16(&e, 0);
            depth++;
            break;
        case ']':
            if (depth == 0) return bf_fail(err, errcap, line, "unmatched ']'");
            depth--;
            emit8(&e, 0xC3);                                    /* JMP start */
            emit16(&e, bf_stack[depth].start_addr);
            patch16(&e, bf_stack[depth].patch_off, (uint16_t)(BF_ORG + e.len));
            break;
        default:
            break;
        }
        if (e.overflow) return bf_fail(err, errcap, line, "program too large");
    }

    if (depth > 0) return bf_fail(err, errcap, bf_stack[0].line, "unmatched '['");

    emit8(&e, 0x76);                                            /* HLT */
    if (e.overflow) return bf_fail(err, errcap, line, "program too large");

    return (int)e.len;
}
