/* tests/emu/test_v2_cpu.c
 * Spec-derived tests for the Intel 8080 core (src/emu/cpu.h).
 * Behaviors: WS1-04 (cycle table), WS1-05 (opcode lengths and undocumented aliases).
 * Written from .fullsend/SPEC.md only; no implementation was consulted.
 * Every machine run below is a bounded number of explicit cpu_step calls.
 */
#include "../testfw.h"
#include "emu/cpu.h"
#include "emu/mem.h"
#include <stdint.h>
#include <string.h>

/* Intel 8080 cycle counts, conditional CALL/RET in their NOT-taken form. */
static const uint8_t CYC[256] = {
    /* 00 */  4, 10,  7,  5,  5,  5,  7,  4,   4, 10,  7,  5,  5,  5,  7,  4,
    /* 10 */  4, 10,  7,  5,  5,  5,  7,  4,   4, 10,  7,  5,  5,  5,  7,  4,
    /* 20 */  4, 10, 16,  5,  5,  5,  7,  4,   4, 10, 16,  5,  5,  5,  7,  4,
    /* 30 */  4, 10, 13,  5, 10, 10, 10,  4,   4, 10, 13,  5,  5,  5,  7,  4,
    /* 40 */  5,  5,  5,  5,  5,  5,  7,  5,   5,  5,  5,  5,  5,  5,  7,  5,
    /* 50 */  5,  5,  5,  5,  5,  5,  7,  5,   5,  5,  5,  5,  5,  5,  7,  5,
    /* 60 */  5,  5,  5,  5,  5,  5,  7,  5,   5,  5,  5,  5,  5,  5,  7,  5,
    /* 70 */  7,  7,  7,  7,  7,  7,  7,  7,   5,  5,  5,  5,  5,  5,  7,  5,
    /* 80 */  4,  4,  4,  4,  4,  4,  7,  4,   4,  4,  4,  4,  4,  4,  7,  4,
    /* 90 */  4,  4,  4,  4,  4,  4,  7,  4,   4,  4,  4,  4,  4,  4,  7,  4,
    /* A0 */  4,  4,  4,  4,  4,  4,  7,  4,   4,  4,  4,  4,  4,  4,  7,  4,
    /* B0 */  4,  4,  4,  4,  4,  4,  7,  4,   4,  4,  4,  4,  4,  4,  7,  4,
    /* C0 */  5, 10, 10, 10, 11, 11,  7, 11,   5, 10, 10, 10, 11, 17,  7, 11,
    /* D0 */  5, 10, 10, 10, 11, 11,  7, 11,   5, 10, 10, 10, 11, 17,  7, 11,
    /* E0 */  5, 10, 10, 18, 11, 11,  7, 11,   5,  5, 10,  4, 11, 17,  7, 11,
    /* F0 */  5, 10, 10,  4, 11, 11,  7, 11,   5,  5, 10,  4, 11, 17,  7, 11,
};

static int is_cond_call(int op) { return (op & 0xC7) == 0xC4; }
static int is_cond_ret(int op)  { return (op & 0xC7) == 0xC0; }
static int is_rst(int op)       { return (op & 0xC7) == 0xC7; }

static int want_len(int op)
{
    switch (op) {
    case 0x01: case 0x11: case 0x21: case 0x31:            /* LXI */
    case 0x22: case 0x2A: case 0x32: case 0x3A:            /* SHLD LHLD STA LDA */
    case 0xC2: case 0xCA: case 0xD2: case 0xDA:            /* Jcc */
    case 0xE2: case 0xEA: case 0xF2: case 0xFA:
    case 0xC3: case 0xCB:                                  /* JMP, JMP* */
    case 0xC4: case 0xCC: case 0xD4: case 0xDC:            /* Ccc */
    case 0xE4: case 0xEC: case 0xF4: case 0xFC:
    case 0xCD: case 0xDD: case 0xED: case 0xFD:            /* CALL, CALL* */
        return 3;
    case 0x06: case 0x0E: case 0x16: case 0x1E:            /* MVI */
    case 0x26: case 0x2E: case 0x36: case 0x3E:
    case 0xC6: case 0xCE: case 0xD6: case 0xDE:            /* ADI ACI SUI SBI */
    case 0xE6: case 0xEE: case 0xF6: case 0xFE:            /* ANI XRI ORI CPI */
    case 0xD3: case 0xDB:                                  /* OUT IN */
        return 2;
    default:
        return 1;
    }
}

/* Fresh 64K tape + CPU at pc/sp. Registers point at harmless scratch. */
static void fresh(cpu_t *c, uint16_t pc, uint16_t sp)
{
    mem_init(1, 65536);
    memset(c, 0, sizeof *c);
    cpu_init(c);
    c->pc = pc;
    c->sp = sp;
    c->halted = 0;
    c->b = 0x21; c->c = 0x10;      /* BC = 0x2110 */
    c->d = 0x21; c->e = 0x20;      /* DE = 0x2120 */
    c->h = 0x21; c->l = 0x00;      /* HL = 0x2100 */
}

static void put(uint16_t at, const uint8_t *b, int n)
{
    for (int i = 0; i < n; ++i) mem_write((addr_t)(at + i), b[i]);
}

static uint64_t step_delta(cpu_t *c)
{
    uint64_t before = c->cycles;
    cpu_step(c);
    return c->cycles - before;
}

/* ------------------------------------------------------------------ WS1-04 */

static int t_ws1_04_table_untaken(void)
{
    for (int op = 0; op < 256; ++op) {
        unsigned got = cpu_opcode_cycles((uint8_t)op, 0);
        if (got != (unsigned)CYC[op]) {
            fprintf(stderr, "opcode %02X: cpu_opcode_cycles(op,0)=%u expected %u\n", op, got, (unsigned)CYC[op]);
            return 1;
        }
    }
    return 0;
}

static int t_ws1_04_table_taken(void)
{
    for (int op = 0; op < 256; ++op) {
        unsigned want = CYC[op];
        unsigned got;
        if (is_cond_call(op)) want = 17;
        else if (is_cond_ret(op)) want = 11;
        got = cpu_opcode_cycles((uint8_t)op, 1);
        if (got != want) {
            fprintf(stderr, "opcode %02X: cpu_opcode_cycles(op,1)=%u expected %u\n", op, got, want);
            return 1;
        }
    }
    /* `taken` only matters for conditional CALL/RET */
    ASSERT(cpu_opcode_cycles(0xCD, 0) == 17 && cpu_opcode_cycles(0xCD, 1) == 17);   /* CALL */
    ASSERT(cpu_opcode_cycles(0xC9, 0) == 10 && cpu_opcode_cycles(0xC9, 1) == 10);   /* RET */
    ASSERT(cpu_opcode_cycles(0xC2, 0) == 10 && cpu_opcode_cycles(0xC2, 1) == 10);   /* JNZ */
    ASSERT(cpu_opcode_cycles(0xC4, 0) == 11 && cpu_opcode_cycles(0xC4, 1) == 17);   /* CNZ */
    ASSERT(cpu_opcode_cycles(0xC0, 0) == 5  && cpu_opcode_cycles(0xC0, 1) == 11);   /* RNZ */
    return 0;
}

typedef struct { uint8_t b[3]; uint8_t cyc; const char *name; } prog_t;

static const prog_t PROGS[] = {
    {{0x00, 0x00, 0x00},  4, "NOP"},
    {{0x3E, 0x05, 0x00},  7, "MVI A,05H"},
    {{0x36, 0x05, 0x00}, 10, "MVI M,05H"},
    {{0x21, 0x34, 0x12}, 10, "LXI H,1234H"},
    {{0x78, 0x00, 0x00},  5, "MOV A,B"},
    {{0x7E, 0x00, 0x00},  7, "MOV A,M"},
    {{0x77, 0x00, 0x00},  7, "MOV M,A"},
    {{0x80, 0x00, 0x00},  4, "ADD B"},
    {{0x86, 0x00, 0x00},  7, "ADD M"},
    {{0xC6, 0x01, 0x00},  7, "ADI 01H"},
    {{0xFE, 0x01, 0x00},  7, "CPI 01H"},
    {{0x04, 0x00, 0x00},  5, "INR B"},
    {{0x34, 0x00, 0x00}, 10, "INR M"},
    {{0x35, 0x00, 0x00}, 10, "DCR M"},
    {{0x23, 0x00, 0x00},  5, "INX H"},
    {{0x3B, 0x00, 0x00},  5, "DCX SP"},
    {{0x09, 0x00, 0x00}, 10, "DAD B"},
    {{0xC5, 0x00, 0x00}, 11, "PUSH B"},
    {{0xF5, 0x00, 0x00}, 11, "PUSH PSW"},
    {{0xC1, 0x00, 0x00}, 10, "POP B"},
    {{0xE3, 0x00, 0x00}, 18, "XTHL"},
    {{0xEB, 0x00, 0x00},  4, "XCHG"},
    {{0xF9, 0x00, 0x00},  5, "SPHL"},
    {{0xE9, 0x00, 0x00},  5, "PCHL"},
    {{0xC3, 0x00, 0x02}, 10, "JMP 0200H"},
    {{0xCD, 0x00, 0x02}, 17, "CALL 0200H"},
    {{0xC9, 0x00, 0x00}, 10, "RET"},
    {{0xC7, 0x00, 0x00}, 11, "RST 0"},
    {{0xFF, 0x00, 0x00}, 11, "RST 7"},
    {{0x76, 0x00, 0x00},  7, "HLT"},
    {{0xDB, 0x05, 0x00}, 10, "IN 05H"},
    {{0xD3, 0x01, 0x00}, 10, "OUT 01H"},
    {{0x3A, 0x00, 0x20}, 13, "LDA 2000H"},
    {{0x32, 0x00, 0x20}, 13, "STA 2000H"},
    {{0x2A, 0x00, 0x20}, 16, "LHLD 2000H"},
    {{0x22, 0x00, 0x20}, 16, "SHLD 2000H"},
    {{0x0A, 0x00, 0x00},  7, "LDAX B"},
    {{0x12, 0x00, 0x00},  7, "STAX D"},
    {{0x07, 0x00, 0x00},  4, "RLC"},
    {{0x1F, 0x00, 0x00},  4, "RAR"},
    {{0x2F, 0x00, 0x00},  4, "CMA"},
    {{0x3F, 0x00, 0x00},  4, "CMC"},
    {{0x37, 0x00, 0x00},  4, "STC"},
    {{0x27, 0x00, 0x00},  4, "DAA"},
    {{0xFB, 0x00, 0x00},  4, "EI"},
    {{0xF3, 0x00, 0x00},  4, "DI"},
    {{0x20, 0x00, 0x00},  4, "RIM / NOP alias 20H"},
    {{0x30, 0x00, 0x00},  4, "SIM / NOP alias 30H"},
};

static int t_ws1_04_step_advances_cycles(void)
{
    const int n = (int)(sizeof PROGS / sizeof PROGS[0]);
    for (int i = 0; i < n; ++i) {
        cpu_t c;
        uint64_t got;
        fresh(&c, 0x0100, 0x2000);
        mem_write(0x2000, 0x00);           /* [SP] = 0x0200 so RET has somewhere to go */
        mem_write(0x2001, 0x02);
        put(0x0100, PROGS[i].b, 3);
        got = step_delta(&c);
        if (got != (uint64_t)PROGS[i].cyc) {
            fprintf(stderr, "%s: cycles advanced by %lu, expected %u\n", PROGS[i].name,
                    (unsigned long)got, (unsigned)PROGS[i].cyc);
            return 1;
        }
        if (PROGS[i].b[0] == 0x76) ASSERT(c.halted != 0);
        else ASSERT(c.halted == 0);
    }
    return 0;
}

static int t_ws1_04_conditional_cycles(void)
{
    cpu_t c;
    /* carry-based: STC ; CNC (untaken) ; CC (taken) ; HLT ; NOP ; RNC (untaken) ; RC (taken) */
    static const uint8_t carry[] = {
        0x37,               /* 0100 STC          4  */
        0xD4, 0x10, 0x01,   /* 0101 CNC 0110H   11  untaken */
        0xDC, 0x09, 0x01,   /* 0104 CC  0109H   17  taken, pushes 0107 */
        0x76,               /* 0107 HLT          7  */
        0x00,               /* 0108 NOP             */
        0xD0,               /* 0109 RNC          5  untaken */
        0xD8,               /* 010A RC          11  taken -> 0107 */
    };
    /* zero-based: XRA A ; CNZ (untaken) ; CZ (taken) ; HLT ; NOP ; RNZ (untaken) ; RZ (taken) */
    static const uint8_t zero[] = {
        0xAF,               /* 0100 XRA A        4  */
        0xC4, 0x10, 0x01,   /* 0101 CNZ 0110H   11  untaken */
        0xCC, 0x09, 0x01,   /* 0104 CZ  0109H   17  taken */
        0x76,               /* 0107 HLT          7  */
        0x00,               /* 0108 NOP             */
        0xC0,               /* 0109 RNZ          5  untaken */
        0xC8,               /* 010A RZ          11  taken -> 0107 */
    };

    fresh(&c, 0x0100, 0x2000);
    put(0x0100, carry, (int)sizeof carry);
    ASSERT(step_delta(&c) == 4);  ASSERT(c.pc == 0x0101);           /* STC */
    ASSERT(step_delta(&c) == 11); ASSERT(c.pc == 0x0104);           /* CNC untaken */
    ASSERT(c.sp == 0x2000);
    ASSERT(step_delta(&c) == 17); ASSERT(c.pc == 0x0109);           /* CC taken */
    ASSERT(c.sp == 0x1FFE);
    ASSERT(mem_peek(0, 0x1FFE) == 0x07 && mem_peek(0, 0x1FFF) == 0x01);
    ASSERT(step_delta(&c) == 5);  ASSERT(c.pc == 0x010A);           /* RNC untaken */
    ASSERT(c.sp == 0x1FFE);
    ASSERT(step_delta(&c) == 11); ASSERT(c.pc == 0x0107);           /* RC taken */
    ASSERT(c.sp == 0x2000);
    ASSERT(step_delta(&c) == 7);  ASSERT(c.halted != 0);            /* HLT */

    fresh(&c, 0x0100, 0x2000);
    put(0x0100, zero, (int)sizeof zero);
    ASSERT(step_delta(&c) == 4);  ASSERT(c.pc == 0x0101);           /* XRA A */
    ASSERT(c.a == 0);
    ASSERT(step_delta(&c) == 11); ASSERT(c.pc == 0x0104);           /* CNZ untaken */
    ASSERT(step_delta(&c) == 17); ASSERT(c.pc == 0x0109);           /* CZ taken */
    ASSERT(c.sp == 0x1FFE);
    ASSERT(step_delta(&c) == 5);  ASSERT(c.pc == 0x010A);           /* RNZ untaken */
    ASSERT(step_delta(&c) == 11); ASSERT(c.pc == 0x0107);           /* RZ taken */
    ASSERT(c.sp == 0x2000);
    ASSERT(step_delta(&c) == 7);  ASSERT(c.halted != 0);            /* HLT */
    return 0;
}

/* ------------------------------------------------------------------ WS1-05 */

static int t_ws1_05_len_table(void)
{
    for (int op = 0; op < 256; ++op) {
        unsigned len = cpu_opcode_len((uint8_t)op);
        if (len < 1 || len > 3 || len != (unsigned)want_len(op)) {
            fprintf(stderr, "opcode %02X: cpu_opcode_len=%u expected %d\n", op, len, want_len(op));
            return 1;
        }
    }
    return 0;
}

/* Every opcode executed once from P with operands/stack/HL arranged so that the
 * documented effect leaves pc == P + len (jump targets = P+3, [SP] = P+1, HL = P+1 for PCHL).
 * RST n vectors to 8n and pushes P+1; HLT is skipped (pc after halt is not specified). */
static int t_ws1_05_every_opcode_advances_pc(void)
{
    const uint16_t P = 0x0100, S = 0x2000;
    for (int op = 0; op < 256; ++op) {
        cpu_t c;
        uint8_t prog[3];
        unsigned len = cpu_opcode_len((uint8_t)op);
        if (op == 0x76) continue;
        fresh(&c, P, S);
        if (op == 0xE9) { c.h = 0x01; c.l = 0x01; }         /* PCHL: HL = P+1 */
        mem_write(S, 0x01);                                 /* [SP] = 0x0101 = P+1 */
        mem_write((addr_t)(S + 1), 0x01);
        prog[0] = (uint8_t)op;
        prog[1] = 0x03;                                     /* imm16 = 0x0103 = P+3 */
        prog[2] = 0x01;
        put(P, prog, 3);
        cpu_step(&c);
        if (is_rst(op)) {
            uint16_t vec = (uint16_t)(((op >> 3) & 7) * 8);
            if (c.pc != vec || c.sp != (uint16_t)(S - 2) ||
                mem_peek(0, (addr_t)(S - 2)) != 0x01 || mem_peek(0, (addr_t)(S - 1)) != 0x01) {
                fprintf(stderr, "RST opcode %02X: pc=%04X sp=%04X, expected pc=%04X sp=%04X with 0101H pushed\n",
                        op, (unsigned)c.pc, (unsigned)c.sp, (unsigned)vec, (unsigned)(S - 2));
                return 1;
            }
        } else if (c.pc != (uint16_t)(P + len)) {
            fprintf(stderr, "opcode %02X: pc=%04X after one step, expected %04X (len %u)\n",
                    op, (unsigned)c.pc, (unsigned)(P + len), len);
            return 1;
        }
        ASSERT(c.halted == 0);
    }
    return 0;
}

static int t_ws1_05_nop_aliases(void)
{
    static const uint8_t alias[7] = {0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38};
    for (int i = 0; i < 7; ++i) {
        cpu_t c, ref;
        uint8_t p[3];
        fresh(&c, 0x0100, 0x2000);
        c.a = 0x12; c.b = 0x34; c.c = 0x56; c.d = 0x78; c.e = 0x9A; c.h = 0xBC; c.l = 0xDE;
        ref = c;
        p[0] = alias[i]; p[1] = 0x11; p[2] = 0x22;
        put(0x0100, p, 3);
        ASSERT(step_delta(&c) == 4);
        ASSERT(c.pc == 0x0101);
        ASSERT(c.sp == 0x2000);
        ASSERT(c.halted == 0);
        ASSERT(c.b == ref.b && c.c == ref.c && c.d == ref.d && c.e == ref.e && c.h == ref.h && c.l == ref.l);
        ASSERT(c.flags == ref.flags);
        if (alias[i] != 0x20 && alias[i] != 0x30) ASSERT(c.a == ref.a);   /* 20H/30H also serve as RIM/SIM, which touch A */
        ASSERT(mem_peek(0, 0x0101) == 0x11 && mem_peek(0, 0x0102) == 0x22); /* operand bytes untouched */
    }
    return 0;
}

static int t_ws1_05_cb_is_jmp(void)
{
    cpu_t c, r;
    static const uint8_t alias[3] = {0xCB, 0x34, 0x12};
    static const uint8_t real[3]  = {0xC3, 0x34, 0x12};
    fresh(&c, 0x0100, 0x2000);
    put(0x0100, alias, 3);
    ASSERT(step_delta(&c) == 10);
    ASSERT(c.pc == 0x1234);
    ASSERT(c.sp == 0x2000);
    ASSERT(c.halted == 0);
    fresh(&r, 0x0100, 0x2000);
    put(0x0100, real, 3);
    cpu_step(&r);
    ASSERT(r.pc == c.pc && r.sp == c.sp && r.cycles == c.cycles);
    return 0;
}

static int t_ws1_05_d9_is_ret(void)
{
    cpu_t c, r;
    static const uint8_t alias[3] = {0xD9, 0x00, 0x00};
    static const uint8_t real[3]  = {0xC9, 0x00, 0x00};
    fresh(&c, 0x0100, 0x2000);
    mem_write(0x2000, 0x78);
    mem_write(0x2001, 0x56);
    put(0x0100, alias, 3);
    ASSERT(step_delta(&c) == 10);
    ASSERT(c.pc == 0x5678);
    ASSERT(c.sp == 0x2002);
    ASSERT(c.halted == 0);
    fresh(&r, 0x0100, 0x2000);
    mem_write(0x2000, 0x78);
    mem_write(0x2001, 0x56);
    put(0x0100, real, 3);
    cpu_step(&r);
    ASSERT(r.pc == c.pc && r.sp == c.sp && r.cycles == c.cycles);
    return 0;
}

static int t_ws1_05_dd_ed_fd_are_call(void)
{
    static const uint8_t ops[3] = {0xDD, 0xED, 0xFD};
    for (int i = 0; i < 3; ++i) {
        cpu_t c;
        uint8_t p[3];
        fresh(&c, 0x0100, 0x2000);
        p[0] = ops[i]; p[1] = 0x00; p[2] = 0x30;
        put(0x0100, p, 3);
        ASSERT(step_delta(&c) == 17);
        ASSERT(c.pc == 0x3000);
        ASSERT(c.sp == 0x1FFE);
        ASSERT(mem_peek(0, 0x1FFE) == 0x03);      /* return address 0x0103, little-endian */
        ASSERT(mem_peek(0, 0x1FFF) == 0x01);
        ASSERT(c.halted == 0);
    }
    return 0;
}

int main(void)
{
    TEST("WS1-04: cpu_opcode_cycles(op,0) matches Intel's table for all 256 opcodes", t_ws1_04_table_untaken);
    TEST("WS1-04: cpu_opcode_cycles(op,1) is 17/11 for conditional CALL/RET and unchanged elsewhere", t_ws1_04_table_taken);
    TEST("WS1-04: cpu_step advances cycles by the table amount", t_ws1_04_step_advances_cycles);
    TEST("WS1-04: conditional CALL is 11/17 and conditional RET is 5/11 when executed", t_ws1_04_conditional_cycles);
    TEST("WS1-05: cpu_opcode_len is 1..3 and matches the 8080 encoding for every opcode", t_ws1_05_len_table);
    TEST("WS1-05: a program of any single opcode advances pc by cpu_opcode_len", t_ws1_05_every_opcode_advances_pc);
    TEST("WS1-05: aliases 08,10,18,20,28,30,38 act as NOP", t_ws1_05_nop_aliases);
    TEST("WS1-05: alias CB acts as JMP", t_ws1_05_cb_is_jmp);
    TEST("WS1-05: alias D9 acts as RET", t_ws1_05_d9_is_ret);
    TEST("WS1-05: aliases DD,ED,FD act as CALL", t_ws1_05_dd_ed_fd_are_call);
    printf("PASS: test_v2_cpu\n");
    RUN_ALL_TESTS();
}
