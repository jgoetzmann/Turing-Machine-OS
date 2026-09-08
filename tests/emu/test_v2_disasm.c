/* tests/emu/test_v2_disasm.c
 * Spec-derived tests for the disassembler (src/emu/disasm.h).
 * Behavior: WS1-08 (disasm_one formatting and lengths; cross-checked with cpu_opcode_len, WS1-05).
 * Written from .fullsend/SPEC.md only; no implementation was consulted.
 */
#include "../testfw.h"
#include "emu/disasm.h"
#include "emu/cpu.h"
#include <stdint.h>
#include <string.h>

/* 1 when disasm_one(b) yields exactly `want` with length `wantlen`, else prints and returns 0 */
static int expect(const uint8_t *b, const char *want, int wantlen)
{
    char out[64];
    int n;
    memset(out, 0, sizeof out);
    n = disasm_one(b, 0x0100, out, (int)sizeof out);
    if (n != wantlen || strcmp(out, want) != 0) {
        fprintf(stderr, "disasm %02X %02X %02X: got \"%s\" len %d, want \"%s\" len %d\n",
                b[0], b[1], b[2], out, n, want, wantlen);
        return 0;
    }
    return 1;
}

#define EXPECT3(b0, b1, b2, want, len)                     \
    do {                                                   \
        const uint8_t _b[3] = {(b0), (b1), (b2)};          \
        ASSERT(expect(_b, (want), (len)));                 \
    } while (0)

/* ------------------------------------------------------------------ WS1-08 */

static int t_ws1_08_spec_examples(void)
{
    EXPECT3(0x3E, 0x05, 0x00, "MVI A,05H", 2);
    EXPECT3(0x21, 0x34, 0x12, "LXI H,1234H", 3);
    EXPECT3(0xC3, 0x23, 0x01, "JMP 0123H", 3);
    EXPECT3(0x7E, 0x00, 0x00, "MOV A,M", 1);
    EXPECT3(0xDF, 0x00, 0x00, "RST 3", 1);
    EXPECT3(0x00, 0x00, 0x00, "NOP", 1);
    EXPECT3(0x76, 0x00, 0x00, "HLT", 1);
    return 0;
}

static int t_ws1_08_nop_alias_star(void)
{
    /* Each alias byte spells itself, so a listing assembles back to the same bytes. */
    EXPECT3(0x08, 0x11, 0x22, "NOP*", 1);
    EXPECT3(0x10, 0x11, 0x22, "NOP*10", 1);
    EXPECT3(0x18, 0x11, 0x22, "NOP*18", 1);
    EXPECT3(0x20, 0x11, 0x22, "NOP*20", 1);   /* 8085 RIM; a NOP here, like the executor */
    EXPECT3(0x28, 0x11, 0x22, "NOP*28", 1);
    EXPECT3(0x30, 0x11, 0x22, "NOP*30", 1);   /* 8085 SIM */
    EXPECT3(0x38, 0x11, 0x22, "NOP*38", 1);
    return 0;
}

static int t_ws1_08_jmp_alias_star(void)
{
    EXPECT3(0xCB, 0x23, 0x01, "JMP* 0123H", 3);
    EXPECT3(0xCB, 0x00, 0x20, "JMP* 2000H", 3);
    /* the documented opcode has no star */
    EXPECT3(0xC3, 0x00, 0x20, "JMP 2000H", 3);
    return 0;
}

static int t_ws1_08_ret_alias_star(void)
{
    EXPECT3(0xD9, 0x11, 0x22, "RET*", 1);
    EXPECT3(0xC9, 0x11, 0x22, "RET", 1);
    return 0;
}

static int t_ws1_08_call_alias_star(void)
{
    EXPECT3(0xDD, 0x23, 0x01, "CALL* 0123H", 3);
    EXPECT3(0xED, 0x23, 0x01, "CALL*ED 0123H", 3);
    EXPECT3(0xFD, 0x23, 0x01, "CALL*FD 0123H", 3);
    EXPECT3(0xCD, 0x23, 0x01, "CALL 0123H", 3);
    return 0;
}

static int t_ws1_08_more_formats(void)
{
    /* register / register-pair operands, no spaces after the comma */
    EXPECT3(0x41, 0x00, 0x00, "MOV B,C", 1);
    EXPECT3(0x77, 0x00, 0x00, "MOV M,A", 1);
    EXPECT3(0x86, 0x00, 0x00, "ADD M", 1);
    EXPECT3(0xAF, 0x00, 0x00, "XRA A", 1);
    EXPECT3(0x34, 0x00, 0x00, "INR M", 1);
    EXPECT3(0x23, 0x00, 0x00, "INX H", 1);
    EXPECT3(0x39, 0x00, 0x00, "DAD SP", 1);
    EXPECT3(0xC5, 0x00, 0x00, "PUSH B", 1);
    EXPECT3(0xF5, 0x00, 0x00, "PUSH PSW", 1);
    EXPECT3(0xC1, 0x00, 0x00, "POP B", 1);
    EXPECT3(0x0A, 0x00, 0x00, "LDAX B", 1);
    EXPECT3(0x12, 0x00, 0x00, "STAX D", 1);
    EXPECT3(0xEB, 0x00, 0x00, "XCHG", 1);
    EXPECT3(0xE3, 0x00, 0x00, "XTHL", 1);
    EXPECT3(0xE9, 0x00, 0x00, "PCHL", 1);
    EXPECT3(0xF9, 0x00, 0x00, "SPHL", 1);
    EXPECT3(0x27, 0x00, 0x00, "DAA", 1);
    EXPECT3(0xFB, 0x00, 0x00, "EI", 1);
    EXPECT3(0xF3, 0x00, 0x00, "DI", 1);
    EXPECT3(0xC7, 0x00, 0x00, "RST 0", 1);
    EXPECT3(0xFF, 0x00, 0x00, "RST 7", 1);
    EXPECT3(0xC0, 0x00, 0x00, "RNZ", 1);
    /* 8-bit immediates: two hex digits + H */
    EXPECT3(0x06, 0x10, 0x00, "MVI B,10H", 2);
    EXPECT3(0x36, 0x00, 0x00, "MVI M,00H", 2);
    EXPECT3(0xC6, 0x01, 0x00, "ADI 01H", 2);
    EXPECT3(0xFE, 0x05, 0x00, "CPI 05H", 2);
    EXPECT3(0xE6, 0x0F, 0x00, "ANI 0FH", 2);
    EXPECT3(0xD3, 0x01, 0x00, "OUT 01H", 2);
    EXPECT3(0xDB, 0x05, 0x00, "IN 05H", 2);
    /* 16-bit immediates: four hex digits + H, little-endian bytes */
    EXPECT3(0x01, 0x00, 0x01, "LXI B,0100H", 3);
    EXPECT3(0x31, 0x34, 0x12, "LXI SP,1234H", 3);
    EXPECT3(0x32, 0x34, 0x12, "STA 1234H", 3);
    EXPECT3(0x3A, 0x34, 0x12, "LDA 1234H", 3);
    EXPECT3(0x22, 0x34, 0x12, "SHLD 1234H", 3);
    EXPECT3(0x2A, 0x34, 0x12, "LHLD 1234H", 3);
    EXPECT3(0xC2, 0x23, 0x01, "JNZ 0123H", 3);
    EXPECT3(0xC4, 0x23, 0x01, "CNZ 0123H", 3);
    return 0;
}

/* Every opcode: returned length equals cpu_opcode_len, text is a non-empty UPPERCASE
 * mnemonic with at most one space and no space after a comma. Operand bytes are
 * chosen so no hex letters appear in the text. */
static int t_ws1_08_all_opcodes_len_and_shape(void)
{
    for (int op = 0; op < 256; ++op) {
        uint8_t b[3];
        char out[64];
        int n, spaces = 0;
        b[0] = (uint8_t)op; b[1] = 0x23; b[2] = 0x01;
        memset(out, 0, sizeof out);
        n = disasm_one(b, 0x0100, out, (int)sizeof out);
        if (n != (int)cpu_opcode_len((uint8_t)op)) {
            fprintf(stderr, "opcode %02X: disasm_one len %d != cpu_opcode_len %u\n", op, n, (unsigned)cpu_opcode_len((uint8_t)op));
            return 1;
        }
        if (out[0] < 'A' || out[0] > 'Z') {
            fprintf(stderr, "opcode %02X: text \"%s\" does not start with an uppercase mnemonic\n", op, out);
            return 1;
        }
        for (int i = 0; out[i]; ++i) {
            if (out[i] >= 'a' && out[i] <= 'z') {
                fprintf(stderr, "opcode %02X: text \"%s\" contains lowercase\n", op, out);
                return 1;
            }
            if (out[i] == ' ') spaces++;
            if (out[i] == ',' && out[i + 1] == ' ') {
                fprintf(stderr, "opcode %02X: text \"%s\" has a space after a comma\n", op, out);
                return 1;
            }
        }
        if (spaces > 1 || out[strlen(out) - 1] == ' ') {
            fprintf(stderr, "opcode %02X: text \"%s\" has stray spaces\n", op, out);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    TEST("WS1-08: disasm_one formats the spec examples with the right lengths", t_ws1_08_spec_examples);
    TEST("WS1-08: undocumented NOP aliases disassemble as NOP*", t_ws1_08_nop_alias_star);
    TEST("WS1-08: CB disassembles as JMP* with its 16-bit target", t_ws1_08_jmp_alias_star);
    TEST("WS1-08: D9 disassembles as RET*", t_ws1_08_ret_alias_star);
    TEST("WS1-08: DD/ED/FD disassemble as CALL* with their 16-bit target", t_ws1_08_call_alias_star);
    TEST("WS1-08: register, 8-bit and 16-bit operand formats per disasm.h", t_ws1_08_more_formats);
    TEST("WS1-08: every opcode's length equals cpu_opcode_len and the text is an uppercase mnemonic", t_ws1_08_all_opcodes_len_and_shape);
    printf("PASS: test_v2_disasm\n");
    RUN_ALL_TESTS();
}
