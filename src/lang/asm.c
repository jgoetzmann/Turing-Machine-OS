/* TuringOS v2 - 8080 two-pass assembler (SPEC S4; behaviors WS7-01, WS6-07).
 *
 * Freestanding C99: no host I/O headers, no malloc, every table is static.
 * Limits: source <= 32 KB, symbols <= 512, output <= 16128 bytes (the TPA).
 *
 * Syntax, one statement per line:  [label:] [mnemonic operands] [; comment]
 *   - mnemonics, registers and directives are case-insensitive; labels are case-sensitive
 *   - directives: ORG expr | DB item{,item} | DW expr{,expr} | DS expr | name EQU expr | END
 *   - numbers: 123, 0FFH, 0xFF, $FF, 1010B, 17Q / 17O, 99D, 'A';  `$` alone = current address
 *   - expressions: + - * / evaluated strictly left to right, parentheses, unary + and -
 *   - forward references resolve in pass 2; ORG / DS / EQU operands must be known in pass 1
 *   - a line shaped like disasm output ("AAAA: BB BB BB  MNEMONIC") is accepted: the listed
 *     bytes are placed verbatim at address AAAA so a disassembly reassembles byte-identically;
 *     the undocumented-alias spellings NOP* / JMP* / RET* / CALL* are accepted as mnemonics too
 *   - output: the bytes from the first emitted address (the ORG in effect) to the last emitted
 *     byte; gaps left by a forward ORG or by DS are zero-filled
 *
 * Errors (first one wins): "line N: unknown mnemonic 'X'", "line N: undefined symbol 'X'",
 * "line N: bad operand", "line N: value out of range", plus "duplicate symbol 'X'",
 * "too many symbols", "symbol too long 'X'", "line too long", "source too large",
 * "program too large" for limit violations.
 */
#include "lang/asm.h"
#include <stdint.h>
#include <string.h>

#define ASM_SRC_MAX     32768u
#define ASM_OUT_MAX     16128u
#define ASM_SYM_MAX     512
#define ASM_NAME_MAX    31
#define ASM_LINE_MAX    1024u
#define ASM_OPS_MAX     64
#define ASM_ORG_DEFAULT 0x0100u

enum {
    K_NONE,   /* no operands, one opcode byte                        */
    K_MOV,    /* MOV r,r                                             */
    K_MVI,    /* MVI r,imm8                                          */
    K_LXI,    /* LXI rp,imm16   (B D H SP)                           */
    K_IMM16,  /* opcode + 16-bit little-endian                       */
    K_IMM8,   /* opcode + 8-bit                                      */
    K_SRC,    /* opcode | r          (ADD ADC SUB SBB ANA XRA ORA CMP)*/
    K_DST,    /* opcode | r << 3     (INR DCR)                       */
    K_RP,     /* opcode | rp << 4    (B D H SP)                      */
    K_RPPSW,  /* opcode | rp << 4    (B D H PSW)                     */
    K_RPBD,   /* opcode | rp << 4    (B D only)                      */
    K_RST,    /* RST n, n = 0..7                                     */
    D_ORG, D_DB, D_DW, D_DS, D_EQU, D_END
};

typedef struct { const char *name; uint8_t kind; uint8_t op; } mn_t;
typedef struct { char name[ASM_NAME_MAX + 1]; int32_t value; } sym_t;
typedef struct { const char *p; } ex_t;

static const mn_t g_mn[] = {
    { "MOV",  K_MOV,   0x40 }, { "MVI",  K_MVI,   0x06 }, { "LXI",  K_LXI,   0x01 },
    { "LDA",  K_IMM16, 0x3A }, { "STA",  K_IMM16, 0x32 },
    { "LHLD", K_IMM16, 0x2A }, { "SHLD", K_IMM16, 0x22 },
    { "LDAX", K_RPBD,  0x0A }, { "STAX", K_RPBD,  0x02 }, { "XCHG", K_NONE,  0xEB },
    { "ADD",  K_SRC,   0x80 }, { "ADC",  K_SRC,   0x88 }, { "SUB",  K_SRC,   0x90 },
    { "SBB",  K_SRC,   0x98 }, { "ANA",  K_SRC,   0xA0 }, { "XRA",  K_SRC,   0xA8 },
    { "ORA",  K_SRC,   0xB0 }, { "CMP",  K_SRC,   0xB8 },
    { "ADI",  K_IMM8,  0xC6 }, { "ACI",  K_IMM8,  0xCE }, { "SUI",  K_IMM8,  0xD6 },
    { "SBI",  K_IMM8,  0xDE }, { "ANI",  K_IMM8,  0xE6 }, { "XRI",  K_IMM8,  0xEE },
    { "ORI",  K_IMM8,  0xF6 }, { "CPI",  K_IMM8,  0xFE },
    { "INR",  K_DST,   0x04 }, { "DCR",  K_DST,   0x05 },
    { "INX",  K_RP,    0x03 }, { "DCX",  K_RP,    0x0B }, { "DAD",  K_RP,    0x09 },
    { "PUSH", K_RPPSW, 0xC5 }, { "POP",  K_RPPSW, 0xC1 },
    { "DAA",  K_NONE,  0x27 }, { "CMA",  K_NONE,  0x2F }, { "STC",  K_NONE,  0x37 },
    { "CMC",  K_NONE,  0x3F }, { "RLC",  K_NONE,  0x07 }, { "RRC",  K_NONE,  0x0F },
    { "RAL",  K_NONE,  0x17 }, { "RAR",  K_NONE,  0x1F },
    { "JMP",  K_IMM16, 0xC3 }, { "JNZ",  K_IMM16, 0xC2 }, { "JZ",   K_IMM16, 0xCA },
    { "JNC",  K_IMM16, 0xD2 }, { "JC",   K_IMM16, 0xDA }, { "JPO",  K_IMM16, 0xE2 },
    { "JPE",  K_IMM16, 0xEA }, { "JP",   K_IMM16, 0xF2 }, { "JM",   K_IMM16, 0xFA },
    { "CALL", K_IMM16, 0xCD }, { "CNZ",  K_IMM16, 0xC4 }, { "CZ",   K_IMM16, 0xCC },
    { "CNC",  K_IMM16, 0xD4 }, { "CC",   K_IMM16, 0xDC }, { "CPO",  K_IMM16, 0xE4 },
    { "CPE",  K_IMM16, 0xEC }, { "CP",   K_IMM16, 0xF4 }, { "CM",   K_IMM16, 0xFC },
    { "RET",  K_NONE,  0xC9 }, { "RNZ",  K_NONE,  0xC0 }, { "RZ",   K_NONE,  0xC8 },
    { "RNC",  K_NONE,  0xD0 }, { "RC",   K_NONE,  0xD8 }, { "RPO",  K_NONE,  0xE0 },
    { "RPE",  K_NONE,  0xE8 }, { "RP",   K_NONE,  0xF0 }, { "RM",   K_NONE,  0xF8 },
    { "RST",  K_RST,   0xC7 }, { "PCHL", K_NONE,  0xE9 },
    { "XTHL", K_NONE,  0xE3 }, { "SPHL", K_NONE,  0xF9 },
    { "IN",   K_IMM8,  0xDB }, { "OUT",  K_IMM8,  0xD3 },
    { "EI",   K_NONE,  0xFB }, { "DI",   K_NONE,  0xF3 },
    { "HLT",  K_NONE,  0x76 }, { "NOP",  K_NONE,  0x00 },
    { "RIM",  K_NONE,  0x20 }, { "SIM",  K_NONE,  0x30 },
    /* undocumented aliases exactly as disasm_one spells them */
    { "NOP*",  K_NONE,  0x08 }, { "JMP*",  K_IMM16, 0xCB },
    { "RET*",  K_NONE,  0xD9 }, { "CALL*", K_IMM16, 0xDD },
    /* directives */
    { "ORG", D_ORG, 0 }, { "DB", D_DB, 0 }, { "DW", D_DW, 0 },
    { "DS",  D_DS,  0 }, { "EQU", D_EQU, 0 }, { "END", D_END, 0 }
};

/* ---- assembler state (all static, reset by asm_assemble) ------------------ */

static char        g_src[ASM_SRC_MAX + 1];
static char        g_lbuf[ASM_LINE_MAX + 1];
static uint8_t     g_img[ASM_OUT_MAX];
static sym_t       g_syms[ASM_SYM_MAX];
static int         g_nsyms;
static int         g_pass;       /* 1 = collect symbols, 2 = emit            */
static uint32_t    g_addr;       /* location counter                          */
static uint32_t    g_base;       /* address of the first emitted byte (pass 2)*/
static int         g_base_set;
static uint32_t    g_len;        /* bytes used in g_img                       */
static int         g_line;       /* 1-based line being processed              */
static int         g_err;        /* set once the first error is recorded      */
static char       *g_errbuf;
static uint32_t    g_errcap;
static int         g_undef;      /* current expression used an undefined name */
static const char *g_undef_s;
static int         g_undef_n;

/* ---- character classes ---------------------------------------------------- */

static int c_space(int c)   { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
static int c_digit(int c)   { return c >= '0' && c <= '9'; }
static int c_alpha(int c)   { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int c_xdigit(int c)  { return c_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static int c_upper(int c)   { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int c_idstart(int c) { return c_alpha(c) || c == '_' || c == '?' || c == '@' || c == '.'; }
static int c_idchar(int c)  { return c_idstart(c) || c_digit(c); }

static int c_xval(int c)
{
    if (c_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

/* ---- errors --------------------------------------------------------------- */

static void err_put(uint32_t *pos, const char *s, int n)
{
    while (n > 0 && *s) {
        if (*pos + 1 < g_errcap) {
            g_errbuf[*pos] = *s;
            (*pos)++;
        }
        s++;
        n--;
    }
}

/* Records "line N: msg" (plus " 'arg'" when arg is given). Only the first error sticks. */
static void err_set(const char *msg, const char *arg, int argn)
{
    uint32_t pos = 0;
    char num[12], rev[12];
    int k = 0, j = 0, v;

    if (g_err) return;
    g_err = 1;
    if (!g_errbuf || g_errcap == 0) return;

    err_put(&pos, "line ", 5);
    v = g_line;
    if (v <= 0) {
        num[k++] = '0';
    } else {
        while (v > 0 && j < 11) { rev[j++] = (char)('0' + v % 10); v /= 10; }
        while (j > 0) num[k++] = rev[--j];
    }
    err_put(&pos, num, k);
    err_put(&pos, ": ", 2);
    err_put(&pos, msg, (int)strlen(msg));
    if (arg) {
        err_put(&pos, " '", 2);
        err_put(&pos, arg, argn);
        err_put(&pos, "'", 1);
    }
    g_errbuf[pos] = 0;
}

static void bad_operand(void) { err_set("bad operand", 0, 0); }
static void out_of_range(void) { err_set("value out of range", 0, 0); }

/* ---- emission ------------------------------------------------------------- */

static void emit(uint8_t b)
{
    if (g_err) return;
    if (g_addr > 0xFFFFu) { out_of_range(); return; }
    if (g_pass == 2) {
        uint32_t off;
        if (!g_base_set) { g_base = g_addr; g_base_set = 1; }
        if (g_addr < g_base) { out_of_range(); return; }
        off = g_addr - g_base;
        if (off >= ASM_OUT_MAX) { err_set("program too large", 0, 0); return; }
        g_img[off] = b;
        if (off + 1 > g_len) g_len = off + 1;
    }
    g_addr++;
}

/* ---- symbols -------------------------------------------------------------- */

static sym_t *sym_find(const char *name, int n)
{
    int i;
    if (n <= 0 || n > ASM_NAME_MAX) return 0;
    for (i = 0; i < g_nsyms; i++) {
        if ((int)strlen(g_syms[i].name) == n && memcmp(g_syms[i].name, name, (size_t)n) == 0)
            return &g_syms[i];
    }
    return 0;
}

static void sym_define(const char *name, int n, int32_t v, int allow_redef)
{
    sym_t *s;
    if (n > ASM_NAME_MAX) { err_set("symbol too long", name, n); return; }
    s = sym_find(name, n);
    if (s) {
        if (allow_redef) { s->value = v; return; }
        err_set("duplicate symbol", name, n);
        return;
    }
    if (g_nsyms >= ASM_SYM_MAX) { err_set("too many symbols", 0, 0); return; }
    s = &g_syms[g_nsyms++];
    memcpy(s->name, name, (size_t)n);
    s->name[n] = 0;
    s->value = v;
}

/* ---- escapes and strings -------------------------------------------------- */

/* *pp points just after a backslash; consumes the escape and returns the byte. */
static int esc_char(const char **pp)
{
    int c = (unsigned char)**pp;
    (*pp)++;
    switch (c) {
    case 'n': return 10;
    case 'r': return 13;
    case 't': return 9;
    case '0': return 0;
    case 'a': return 7;
    case 'b': return 8;
    case 'e': return 27;
    default:  return c;
    }
}

/* s points at an opening quote; returns the pointer just past the closing quote, or 0.
 * A doubled quote inside the string stands for one quote character. */
static const char *str_scan(const char *s)
{
    int q = (unsigned char)*s++;
    for (;;) {
        int c = (unsigned char)*s;
        if (c == 0) return 0;
        if (c == q) {
            if ((unsigned char)s[1] == q) { s += 2; continue; }
            return s + 1;
        }
        if (c == '\\' && s[1]) { s += 2; continue; }
        s++;
    }
}

/* Emits the bytes of a quoted string previously validated by str_scan. */
static void db_string(const char *s)
{
    int q = (unsigned char)*s++;
    for (;;) {
        int c = (unsigned char)*s;
        if (c == 0) return;
        if (c == q) {
            if ((unsigned char)s[1] == q) { s += 2; emit((uint8_t)q); continue; }
            return;
        }
        if (c == '\\' && s[1]) { s++; c = esc_char(&s); emit((uint8_t)c); continue; }
        s++;
        emit((uint8_t)c);
    }
}

/* ---- expressions ---------------------------------------------------------- */

static void ex_ws(ex_t *x) { while (c_space((unsigned char)*x->p)) x->p++; }

static int32_t ex_expr(ex_t *x);

/* Numeric literal starting with a digit: 123, 0FFH, 0xFF, 1010B, 17Q, 17O, 10D. */
static int32_t ex_number(ex_t *x)
{
    const char *s = x->p;
    int n, end, base = 10, i;
    uint32_t v = 0;

    while (c_alpha((unsigned char)*x->p) || c_digit((unsigned char)*x->p)) x->p++;
    n = (int)(x->p - s);
    end = n;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16; s += 2; end = n - 2;
    } else if (n >= 2) {
        int last = c_upper((unsigned char)s[n - 1]);
        if (last == 'H')                     { base = 16; end = n - 1; }
        else if (last == 'B')                { base = 2;  end = n - 1; }
        else if (last == 'D')                { base = 10; end = n - 1; }
        else if (last == 'O' || last == 'Q') { base = 8;  end = n - 1; }
    }
    if (end <= 0) { bad_operand(); return 0; }
    for (i = 0; i < end; i++) {
        int d;
        if (!c_xdigit((unsigned char)s[i])) { bad_operand(); return 0; }
        d = c_xval((unsigned char)s[i]);
        if (d >= base) { bad_operand(); return 0; }
        v = v * (uint32_t)base + (uint32_t)d;
        if (v > 0x0FFFFFFFu) { out_of_range(); return 0; }
    }
    return (int32_t)v;
}

/* 'A' or "A" (one or two characters; two characters form a big-endian 16-bit value). */
static int32_t ex_charlit(ex_t *x)
{
    int q = (unsigned char)*x->p++;
    int32_t v = 0;
    int cnt = 0;
    for (;;) {
        int c = (unsigned char)*x->p;
        if (c == 0) { bad_operand(); return 0; }
        if (c == q) {
            if ((unsigned char)x->p[1] == q) { x->p += 2; }
            else { x->p++; break; }
        } else if (c == '\\' && x->p[1]) {
            x->p++;
            c = esc_char(&x->p);
        } else {
            x->p++;
        }
        if (cnt >= 2) { out_of_range(); return 0; }
        v = (int32_t)(((uint32_t)v << 8) | (uint32_t)(c & 0xFF));
        cnt++;
    }
    if (cnt == 0) { bad_operand(); return 0; }
    return v;
}

static int32_t ex_primary(ex_t *x)
{
    int c;
    ex_ws(x);
    c = (unsigned char)*x->p;

    if (c == '$') {
        if (c_xdigit((unsigned char)x->p[1])) {          /* $FF hex literal */
            uint32_t v = 0;
            x->p++;
            while (c_xdigit((unsigned char)*x->p)) {
                v = v * 16u + (uint32_t)c_xval((unsigned char)*x->p);
                x->p++;
                if (v > 0x0FFFFFFFu) { out_of_range(); return 0; }
            }
            return (int32_t)v;
        }
        x->p++;                                           /* $ = current address */
        return (int32_t)g_addr;
    }
    if (c == '\'' || c == '"') return ex_charlit(x);
    if (c_digit(c)) return ex_number(x);
    if (c_idstart(c)) {
        const char *s = x->p;
        int n;
        sym_t *sym;
        while (c_idchar((unsigned char)*x->p)) x->p++;
        n = (int)(x->p - s);
        sym = sym_find(s, n);
        if (sym) return sym->value;
        /* hex constant written with a leading letter and H suffix, e.g. FEH / FE00H
         * (that is how disasm_one prints immediates >= 0xA0) */
        if (n >= 2 && n <= 8 && c_upper((unsigned char)s[n - 1]) == 'H') {
            int i, ok = 1;
            uint32_t v = 0;
            for (i = 0; i < n - 1; i++) {
                if (!c_xdigit((unsigned char)s[i])) { ok = 0; break; }
                v = v * 16u + (uint32_t)c_xval((unsigned char)s[i]);
            }
            if (ok) return (int32_t)v;
        }
        g_undef = 1;
        g_undef_s = s;
        g_undef_n = n;
        if (g_pass == 2) err_set("undefined symbol", s, n);
        return 0;
    }
    bad_operand();
    return 0;
}

static int32_t ex_unary(ex_t *x)
{
    int32_t v;
    ex_ws(x);
    if (*x->p == '-') { x->p++; v = ex_unary(x); return (int32_t)(0u - (uint32_t)v); }
    if (*x->p == '+') { x->p++; return ex_unary(x); }
    if (*x->p == '(') {
        x->p++;
        v = ex_expr(x);
        ex_ws(x);
        if (*x->p != ')') { bad_operand(); return 0; }
        x->p++;
        return v;
    }
    return ex_primary(x);
}

/* + - * / strictly left to right (no precedence), as SPEC S4 says. */
static int32_t ex_expr(ex_t *x)
{
    int32_t v = ex_unary(x);
    for (;;) {
        int c;
        int32_t r;
        if (g_err) return 0;
        ex_ws(x);
        c = (unsigned char)*x->p;
        if (c != '+' && c != '-' && c != '*' && c != '/') break;
        x->p++;
        r = ex_unary(x);
        if (g_err) return 0;
        switch (c) {
        case '+': v = (int32_t)((uint32_t)v + (uint32_t)r); break;
        case '-': v = (int32_t)((uint32_t)v - (uint32_t)r); break;
        case '*': v = (int32_t)((uint32_t)v * (uint32_t)r); break;
        default:
            if (r == 0)       v = 0;
            else if (r == -1) v = (int32_t)(0u - (uint32_t)v);
            else              v = v / r;
            break;
        }
    }
    return v;
}

/* Evaluates a whole NUL-terminated operand; anything left over is a bad operand.
 * Resets g_undef first so callers can tell whether a forward reference was involved. */
static int32_t ex_eval(const char *s)
{
    ex_t x;
    int32_t v;
    g_undef = 0;
    x.p = s;
    v = ex_expr(&x);
    if (g_err) return 0;
    ex_ws(&x);
    if (*x.p) { bad_operand(); return 0; }
    return v;
}

/* Range checks are skipped in pass 1 when the value depends on a not-yet-defined label. */
static int range_known(void) { return !(g_pass == 1 && g_undef); }

static int imm8(const char *s, uint8_t *out)
{
    int32_t v = ex_eval(s);
    if (g_err) return -1;
    if (range_known() && (v < -128 || v > 255)) { out_of_range(); return -1; }
    *out = (uint8_t)((uint32_t)v & 0xFFu);
    return 0;
}

static int imm16(const char *s, uint16_t *out)
{
    int32_t v = ex_eval(s);
    if (g_err) return -1;
    if (range_known() && (v < -32768 || v > 65535)) { out_of_range(); return -1; }
    *out = (uint16_t)((uint32_t)v & 0xFFFFu);
    return 0;
}

/* Operand that must be fully known in pass 1 (ORG, DS, EQU). */
static int known16(const char *s, int32_t *out, int lo, int hi)
{
    int32_t v = ex_eval(s);
    if (g_err) return -1;
    if (g_undef) { err_set("undefined symbol", g_undef_s, g_undef_n); return -1; }
    if (v < lo || v > hi) { out_of_range(); return -1; }
    *out = v;
    return 0;
}

/* ---- line pieces ---------------------------------------------------------- */

/* Cuts the line at the first ';' outside quotes. */
static void strip_comment(char *s)
{
    int q = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (q) {
            if (c == '\\' && s[1]) { s++; continue; }
            if (c == q) q = 0;
            continue;
        }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == ';') { *s = 0; return; }
    }
}

/* Splits s in place on top-level commas (outside quotes and parentheses).
 * Returns the operand count, -1 when there are too many. */
static int split_ops(char *s, char **ops, int max)
{
    int n = 0, q = 0, depth = 0;
    char *start;

    while (c_space((unsigned char)*s)) s++;
    if (!*s) return 0;
    start = s;
    for (;;) {
        int c = (unsigned char)*s;
        if (c == 0 || (c == ',' && !q && depth == 0)) {
            char *e = s;
            while (e > start && c_space((unsigned char)e[-1])) e--;
            *e = 0;
            if (n >= max) return -1;
            ops[n++] = start;
            if (c == 0) return n;
            s++;
            while (c_space((unsigned char)*s)) s++;
            start = s;
            continue;
        }
        if (q) {
            if (c == '\\' && s[1]) { s += 2; continue; }
            if (c == q) {
                if ((unsigned char)s[1] == q) { s += 2; continue; }
                q = 0;
            }
            s++;
            continue;
        }
        if (c == '\'' || c == '"') { q = c; s++; continue; }
        if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; }
        s++;
    }
}

/* Single register B C D E H L M A -> 0..7, else -1. */
static int reg_r(const char *s)
{
    static const char names[] = "BCDEHLMA";
    int c, i;
    if (!s[0] || s[1]) return -1;
    c = c_upper((unsigned char)s[0]);
    for (i = 0; i < 8; i++) if (names[i] == c) return i;
    return -1;
}

/* Register pair B/BC D/DE H/HL -> 0..2, SP or PSW -> 3 when allowed, else -1. */
static int reg_rp(const char *s, int allow_sp, int allow_psw)
{
    char u[4];
    int i;
    for (i = 0; i < 3 && s[i]; i++) u[i] = (char)c_upper((unsigned char)s[i]);
    if (s[i]) return -1;
    u[i] = 0;
    if (!strcmp(u, "B") || !strcmp(u, "BC")) return 0;
    if (!strcmp(u, "D") || !strcmp(u, "DE")) return 1;
    if (!strcmp(u, "H") || !strcmp(u, "HL")) return 2;
    if (allow_sp && !strcmp(u, "SP")) return 3;
    if (allow_psw && !strcmp(u, "PSW")) return 3;
    return -1;
}

static const mn_t *mn_find(const char *s, int n)
{
    size_t i;
    for (i = 0; i < sizeof g_mn / sizeof g_mn[0]; i++) {
        const char *m = g_mn[i].name;
        int j;
        for (j = 0; j < n && m[j]; j++)
            if (c_upper((unsigned char)s[j]) != (int)(unsigned char)m[j]) break;
        if (j == n && m[j] == 0) return &g_mn[i];
    }
    return 0;
}

/* Recognises a disasm listing line "AAAA: BB BB BB  MNEMONIC".
 * Accepted when the address is four hex digits followed by ':' and either the address starts
 * with a digit (a label never does) or at least one "BB " byte pair follows. Fills addr and
 * the listed bytes; *rest points at the mnemonic text. Returns 1 when recognised. */
static int parse_listing(char *line, char **rest, uint8_t *bytes, int *nbytes, uint32_t *addr)
{
    char *p = line, *q;
    uint32_t a = 0;
    int i, nb = 0;

    if (!(c_xdigit((unsigned char)p[0]) && c_xdigit((unsigned char)p[1]) &&
          c_xdigit((unsigned char)p[2]) && c_xdigit((unsigned char)p[3]) && p[4] == ':'))
        return 0;
    if (!(p[5] == ' ' || p[5] == '\t' || p[5] == 0)) return 0;
    for (i = 0; i < 4; i++) a = a * 16u + (uint32_t)c_xval((unsigned char)p[i]);

    q = p + 5;
    while (*q == ' ' || *q == '\t') q++;
    while (nb < 3 && c_xdigit((unsigned char)q[0]) && c_xdigit((unsigned char)q[1]) &&
           (q[2] == ' ' || q[2] == '\t' || q[2] == 0)) {
        bytes[nb++] = (uint8_t)(c_xval((unsigned char)q[0]) * 16 + c_xval((unsigned char)q[1]));
        q += 2;
        if (*q == ' ' && q[1] != ' ' && q[1] != '\t' && q[1] != 0) { q++; continue; }
        break;
    }
    if (nb == 0 && !c_digit((unsigned char)p[0])) return 0;   /* it is a label like DEAD: */
    if (nb == 0) {
        /* unknown byte formatting: skip to just past the first double space, if any */
        char *d = p + 5;
        while (*d && !(d[0] == ' ' && d[1] == ' ')) d++;
        if (*d) q = d;
    }
    while (*q == ' ' || *q == '\t') q++;
    *rest = q;
    *nbytes = nb;
    *addr = a;
    return 1;
}

/* Scans an identifier (optionally ending in '*' for the alias spellings). */
static char *scan_ident(char *p)
{
    while (c_idchar((unsigned char)*p)) p++;
    if (*p == '*') p++;
    return p;
}

static void err_unknown_word(const char *p)
{
    const char *e = p;
    while (*e && !c_space((unsigned char)*e) && *e != ',') e++;
    err_set("unknown mnemonic", p, (int)(e - p));
}

/* ---- one line ------------------------------------------------------------- */

/* Returns 0 to continue, 1 on END, -1 on error. */
static int process_line(char *line)
{
    char *p = line;
    const char *label = 0;
    int nlabel = 0;
    const char *mn = 0;
    int nmn = 0;
    const mn_t *m;
    char *ops[ASM_OPS_MAX];
    int nops, i;
    char *t1;
    int n1;
    uint8_t lb[4];
    int nlb = 0;
    uint32_t laddr = 0;

    if (parse_listing(line, &p, lb, &nlb, &laddr)) {
        g_addr = laddr;
        if (nlb > 0) {
            for (i = 0; i < nlb; i++) emit(lb[i]);
            return g_err ? -1 : 0;
        }
    }

    strip_comment(p);
    while (c_space((unsigned char)*p)) p++;
    if (!*p) return 0;

    if (!c_idstart((unsigned char)*p)) { err_unknown_word(p); return -1; }
    t1 = p;
    p = scan_ident(p);
    n1 = (int)(p - t1);
    while (c_space((unsigned char)*p)) p++;

    if (*p == ':') {                                   /* label: [mnemonic ...] */
        label = t1;
        nlabel = n1;
        p++;
        while (c_space((unsigned char)*p)) p++;
        if (*p) {
            if (!c_idstart((unsigned char)*p)) { err_unknown_word(p); return -1; }
            mn = p;
            p = scan_ident(p);
            nmn = (int)(p - mn);
        }
    } else {
        const mn_t *m1 = mn_find(t1, n1);
        const mn_t *m2 = 0;
        char *t2 = 0, *q = p;
        int n2 = 0;
        if (c_idstart((unsigned char)*q)) {
            t2 = q;
            q = scan_ident(q);
            n2 = (int)(q - t2);
            m2 = mn_find(t2, n2);
        }
        if (m2 && m2->kind == D_EQU) {                 /* name EQU expr */
            label = t1; nlabel = n1; mn = t2; nmn = n2; p = q;
        } else if (m1) {                               /* mnemonic operands */
            mn = t1; nmn = n1;
        } else if (m2) {                               /* label without colon */
            label = t1; nlabel = n1; mn = t2; nmn = n2; p = q;
        } else {
            err_set("unknown mnemonic", t1, n1);
            return -1;
        }
    }
    while (c_space((unsigned char)*p)) p++;

    m = mn ? mn_find(mn, nmn) : 0;
    if (mn && !m) { err_set("unknown mnemonic", mn, nmn); return -1; }

    if (label && !(m && m->kind == D_EQU)) {
        if (g_pass == 1) sym_define(label, nlabel, (int32_t)g_addr, 0);
        if (g_err) return -1;
    }
    if (!m) return 0;                                  /* label-only line */

    nops = split_ops(p, ops, ASM_OPS_MAX);
    if (nops < 0) { bad_operand(); return -1; }

    switch (m->kind) {
    case D_END:
        if (nops > 1) { bad_operand(); return -1; }
        return 1;

    case D_ORG: {
        int32_t v;
        if (nops != 1) { bad_operand(); return -1; }
        if (known16(ops[0], &v, 0, 0xFFFF) < 0) return -1;
        g_addr = (uint32_t)v;
        break;
    }

    case D_DS: {
        int32_t v, k;
        if (nops != 1) { bad_operand(); return -1; }
        if (known16(ops[0], &v, 0, 0xFFFF) < 0) return -1;
        for (k = 0; k < v && !g_err; k++) emit(0);
        break;
    }

    case D_EQU: {
        int32_t v;
        if (!label || nops != 1) { bad_operand(); return -1; }
        if (known16(ops[0], &v, -32768, 65535) < 0) return -1;
        sym_define(label, nlabel, v, g_pass == 2);
        break;
    }

    case D_DB: {
        if (nops == 0) { bad_operand(); return -1; }
        for (i = 0; i < nops && !g_err; i++) {
            const char *s = ops[i];
            uint8_t b;
            if (*s == '\'' || *s == '"') {
                const char *e = str_scan(s);
                if (e) {
                    while (c_space((unsigned char)*e)) e++;
                    if (*e == 0) { db_string(s); continue; }
                }
            }
            if (imm8(s, &b) < 0) return -1;
            emit(b);
        }
        break;
    }

    case D_DW: {
        if (nops == 0) { bad_operand(); return -1; }
        for (i = 0; i < nops && !g_err; i++) {
            uint16_t w;
            if (imm16(ops[i], &w) < 0) return -1;
            emit((uint8_t)(w & 0xFF));
            emit((uint8_t)(w >> 8));
        }
        break;
    }

    case K_NONE:
        if (nops != 0) { bad_operand(); return -1; }
        emit(m->op);
        break;

    case K_MOV: {
        int d, s;
        if (nops != 2) { bad_operand(); return -1; }
        d = reg_r(ops[0]);
        s = reg_r(ops[1]);
        if (d < 0 || s < 0) { bad_operand(); return -1; }
        emit((uint8_t)(0x40 | (d << 3) | s));
        break;
    }

    case K_MVI: {
        int r;
        uint8_t b;
        if (nops != 2) { bad_operand(); return -1; }
        r = reg_r(ops[0]);
        if (r < 0) { bad_operand(); return -1; }
        if (imm8(ops[1], &b) < 0) return -1;
        emit((uint8_t)(0x06 | (r << 3)));
        emit(b);
        break;
    }

    case K_LXI: {
        int rp;
        uint16_t w;
        if (nops != 2) { bad_operand(); return -1; }
        rp = reg_rp(ops[0], 1, 0);
        if (rp < 0) { bad_operand(); return -1; }
        if (imm16(ops[1], &w) < 0) return -1;
        emit((uint8_t)(0x01 | (rp << 4)));
        emit((uint8_t)(w & 0xFF));
        emit((uint8_t)(w >> 8));
        break;
    }

    case K_IMM16: {
        uint16_t w;
        if (nops != 1) { bad_operand(); return -1; }
        if (imm16(ops[0], &w) < 0) return -1;
        emit(m->op);
        emit((uint8_t)(w & 0xFF));
        emit((uint8_t)(w >> 8));
        break;
    }

    case K_IMM8: {
        uint8_t b;
        if (nops != 1) { bad_operand(); return -1; }
        if (imm8(ops[0], &b) < 0) return -1;
        emit(m->op);
        emit(b);
        break;
    }

    case K_SRC: {
        int r;
        if (nops != 1) { bad_operand(); return -1; }
        r = reg_r(ops[0]);
        if (r < 0) { bad_operand(); return -1; }
        emit((uint8_t)(m->op | r));
        break;
    }

    case K_DST: {
        int r;
        if (nops != 1) { bad_operand(); return -1; }
        r = reg_r(ops[0]);
        if (r < 0) { bad_operand(); return -1; }
        emit((uint8_t)(m->op | (r << 3)));
        break;
    }

    case K_RP: {
        int rp;
        if (nops != 1) { bad_operand(); return -1; }
        rp = reg_rp(ops[0], 1, 0);
        if (rp < 0) { bad_operand(); return -1; }
        emit((uint8_t)(m->op | (rp << 4)));
        break;
    }

    case K_RPPSW: {
        int rp;
        if (nops != 1) { bad_operand(); return -1; }
        rp = reg_rp(ops[0], 0, 1);
        if (rp < 0) { bad_operand(); return -1; }
        emit((uint8_t)(m->op | (rp << 4)));
        break;
    }

    case K_RPBD: {
        int rp;
        if (nops != 1) { bad_operand(); return -1; }
        rp = reg_rp(ops[0], 0, 0);
        if (rp < 0 || rp > 1) { bad_operand(); return -1; }
        emit((uint8_t)(m->op | (rp << 4)));
        break;
    }

    case K_RST: {
        int32_t v;
        if (nops != 1) { bad_operand(); return -1; }
        v = ex_eval(ops[0]);
        if (g_err) return -1;
        if (range_known() && (v < 0 || v > 7)) { out_of_range(); return -1; }
        emit((uint8_t)(m->op | ((v & 7) << 3)));
        break;
    }

    default:
        bad_operand();
        return -1;
    }
    return g_err ? -1 : 0;
}

/* ---- entry point ---------------------------------------------------------- */

int asm_assemble(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap)
{
    g_errbuf = err;
    g_errcap = errcap;
    g_err = 0;
    g_line = 0;
    g_undef = 0;
    g_undef_s = 0;
    g_undef_n = 0;
    if (err && errcap) err[0] = 0;

    if (!src) len = 0;
    if (len > ASM_SRC_MAX) { g_line = 1; err_set("source too large", 0, 0); return -1; }
    if (len) memcpy(g_src, src, len);
    g_src[len] = 0;
    memset(g_img, 0, sizeof g_img);
    g_nsyms = 0;
    g_len = 0;

    for (g_pass = 1; g_pass <= 2; g_pass++) {
        uint32_t i = 0;
        g_addr = ASM_ORG_DEFAULT;
        g_base = 0;
        g_base_set = 0;
        g_line = 0;
        while (i < len) {
            uint32_t j = i, n;
            int r;
            while (j < len && g_src[j] != '\n') j++;
            n = j - i;
            g_line++;
            if (n > ASM_LINE_MAX) { err_set("line too long", 0, 0); return -1; }
            memcpy(g_lbuf, g_src + i, n);
            g_lbuf[n] = 0;
            r = process_line(g_lbuf);
            if (r < 0 || g_err) return -1;
            if (r > 0) break;                 /* END */
            i = j + 1;
        }
    }

    if (g_len > cap) { g_line = 0; err_set("program too large", 0, 0); return -1; }
    if (g_len && out) memcpy(out, g_img, g_len);
    return (int)g_len;
}
