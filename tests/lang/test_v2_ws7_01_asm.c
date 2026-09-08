/* WS7-01: 8080 assembler (SPEC S4). Encodings, forward references, DB/DW/DS, ORG, EQU, $, number
 * formats, left-to-right expressions, case rules, END, one assembled program run through the tos_*
 * API, and every error message named in S4. */
#include "../testfw.h"
#include "api/api.h"
#include "lang/asm.h"
#include <stdint.h>
#include <string.h>

#define RUN_BUDGET 1000000u

static uint8_t  g_bin[32768];
static char     g_err[512];
static char     g_out[16384];
static uint32_t g_out_len;
static uint32_t g_steps;

static int assemble(const char *src)
{
    g_err[0] = 0;
    memset(g_bin, 0xEE, sizeof g_bin);
    return asm_assemble(src, (uint32_t)strlen(src), g_bin, (uint32_t)sizeof g_bin, g_err, (uint32_t)sizeof g_err);
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

static int run_prog(const tos_config_t *cfg, const uint8_t *com, uint32_t len, const char *input)
{
    uint32_t total = 0;
    int guard = 100000;
    if (tos_create(cfg) != 0) return -1;
    while (tos_con_pop() >= 0) { }
    if (input) { const char *p = input; while (*p) tos_con_push((uint8_t)*p++); }
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

static int bytes_are(int len, const uint8_t *expect, int n)
{
    return len == n && memcmp(g_bin, expect, (size_t)n) == 0;
}

/* ---- success cases ---- */

static int t_mvi_rst_basic(void)
{
    static const uint8_t e1[] = { 0x3E, 0x05 };
    static const uint8_t e2[] = { 0x3E, 0x05, 0xDF, 0x7E, 0x76 };
    ASSERT(bytes_are(assemble("MVI A,05H\n"), e1, 2));
    ASSERT(bytes_are(assemble("MVI A,05H\nRST 3\nMOV A,M\nHLT\n"), e2, 5));
    return 0;
}

typedef struct { const char *src; uint8_t b[3]; int n; } enc_t;

static int t_opcode_table(void)
{
    static const enc_t T[] = {
        { "NOP",          {0x00,0,0}, 1 }, { "MOV A,B",      {0x78,0,0}, 1 }, { "MOV M,A",      {0x77,0,0}, 1 },
        { "MOV A,M",      {0x7E,0,0}, 1 }, { "MVI M,0",      {0x36,0x00,0}, 2 }, { "MVI B,7",   {0x06,0x07,0}, 2 },
        { "LXI B,1",      {0x01,0x01,0x00}, 3 }, { "LXI D,2",  {0x11,0x02,0x00}, 3 }, { "LXI H,1234H", {0x21,0x34,0x12}, 3 },
        { "LXI SP,3",     {0x31,0x03,0x00}, 3 }, { "LDA 1234H", {0x3A,0x34,0x12}, 3 }, { "STA 1234H", {0x32,0x34,0x12}, 3 },
        { "LHLD 1234H",   {0x2A,0x34,0x12}, 3 }, { "SHLD 2000H", {0x22,0x00,0x20}, 3 }, { "LDAX B", {0x0A,0,0}, 1 },
        { "LDAX D",       {0x1A,0,0}, 1 }, { "STAX B",       {0x02,0,0}, 1 }, { "STAX D",       {0x12,0,0}, 1 },
        { "XCHG",         {0xEB,0,0}, 1 }, { "ADD B",        {0x80,0,0}, 1 }, { "ADD M",        {0x86,0,0}, 1 },
        { "ADI 1",        {0xC6,0x01,0}, 2 }, { "ADC C",      {0x89,0,0}, 1 }, { "ACI 1",        {0xCE,0x01,0}, 2 },
        { "SUB D",        {0x92,0,0}, 1 }, { "SUI 1",        {0xD6,0x01,0}, 2 }, { "SBB E",        {0x9B,0,0}, 1 },
        { "SBI 1",        {0xDE,0x01,0}, 2 }, { "INR A",      {0x3C,0,0}, 1 }, { "INR M",        {0x34,0,0}, 1 },
        { "DCR B",        {0x05,0,0}, 1 }, { "DCR M",        {0x35,0,0}, 1 }, { "INX B",        {0x03,0,0}, 1 },
        { "INX D",        {0x13,0,0}, 1 }, { "INX H",        {0x23,0,0}, 1 }, { "INX SP",       {0x33,0,0}, 1 },
        { "DCX B",        {0x0B,0,0}, 1 }, { "DCX D",        {0x1B,0,0}, 1 }, { "DCX H",        {0x2B,0,0}, 1 },
        { "DCX SP",       {0x3B,0,0}, 1 }, { "DAD B",        {0x09,0,0}, 1 }, { "DAD D",        {0x19,0,0}, 1 },
        { "DAD H",        {0x29,0,0}, 1 }, { "DAD SP",       {0x39,0,0}, 1 }, { "DAA",          {0x27,0,0}, 1 },
        { "ANA H",        {0xA4,0,0}, 1 }, { "ANI 0FH",      {0xE6,0x0F,0}, 2 }, { "ORA M",      {0xB6,0,0}, 1 },
        { "ORI 1",        {0xF6,0x01,0}, 2 }, { "XRA L",      {0xAD,0,0}, 1 }, { "XRI 1",        {0xEE,0x01,0}, 2 },
        { "CMP A",        {0xBF,0,0}, 1 }, { "CPI 'A'",      {0xFE,0x41,0}, 2 }, { "RLC",        {0x07,0,0}, 1 },
        { "RRC",          {0x0F,0,0}, 1 }, { "RAL",          {0x17,0,0}, 1 }, { "RAR",          {0x1F,0,0}, 1 },
        { "CMA",          {0x2F,0,0}, 1 }, { "CMC",          {0x3F,0,0}, 1 }, { "STC",          {0x37,0,0}, 1 },
        { "JMP 0123H",    {0xC3,0x23,0x01}, 3 }, { "JNZ 0100H", {0xC2,0x00,0x01}, 3 }, { "JZ 0100H", {0xCA,0x00,0x01}, 3 },
        { "JNC 0100H",    {0xD2,0x00,0x01}, 3 }, { "JC 0100H",  {0xDA,0x00,0x01}, 3 }, { "JPO 0100H", {0xE2,0x00,0x01}, 3 },
        { "JPE 0100H",    {0xEA,0x00,0x01}, 3 }, { "JP 0100H",  {0xF2,0x00,0x01}, 3 }, { "JM 0100H",  {0xFA,0x00,0x01}, 3 },
        { "CALL 0100H",   {0xCD,0x00,0x01}, 3 }, { "CNZ 0100H", {0xC4,0x00,0x01}, 3 }, { "CZ 0100H",  {0xCC,0x00,0x01}, 3 },
        { "CNC 0100H",    {0xD4,0x00,0x01}, 3 }, { "CC 0100H",  {0xDC,0x00,0x01}, 3 }, { "CPO 0100H", {0xE4,0x00,0x01}, 3 },
        { "CPE 0100H",    {0xEC,0x00,0x01}, 3 }, { "CP 0100H",  {0xF4,0x00,0x01}, 3 }, { "CM 0100H",  {0xFC,0x00,0x01}, 3 },
        { "RET",          {0xC9,0,0}, 1 }, { "RNZ",          {0xC0,0,0}, 1 }, { "RZ",           {0xC8,0,0}, 1 },
        { "RNC",          {0xD0,0,0}, 1 }, { "RC",           {0xD8,0,0}, 1 }, { "RPO",          {0xE0,0,0}, 1 },
        { "RPE",          {0xE8,0,0}, 1 }, { "RP",           {0xF0,0,0}, 1 }, { "RM",           {0xF8,0,0}, 1 },
        { "RST 0",        {0xC7,0,0}, 1 }, { "RST 3",        {0xDF,0,0}, 1 }, { "RST 7",        {0xFF,0,0}, 1 },
        { "PCHL",         {0xE9,0,0}, 1 }, { "PUSH B",       {0xC5,0,0}, 1 }, { "PUSH D",       {0xD5,0,0}, 1 },
        { "PUSH H",       {0xE5,0,0}, 1 }, { "PUSH PSW",     {0xF5,0,0}, 1 }, { "POP B",        {0xC1,0,0}, 1 },
        { "POP D",        {0xD1,0,0}, 1 }, { "POP H",        {0xE1,0,0}, 1 }, { "POP PSW",      {0xF1,0,0}, 1 },
        { "XTHL",         {0xE3,0,0}, 1 }, { "SPHL",         {0xF9,0,0}, 1 }, { "IN 3",         {0xDB,0x03,0}, 2 },
        { "OUT 1",        {0xD3,0x01,0}, 2 }, { "EI",         {0xFB,0,0}, 1 }, { "DI",           {0xF3,0,0}, 1 },
        { "HLT",          {0x76,0,0}, 1 }, { "RIM",          {0x20,0,0}, 1 }, { "SIM",          {0x30,0,0}, 1 },
    };
    size_t i;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        char src[64];
        int len;
        strcpy(src, T[i].src);
        strcat(src, "\n");
        len = assemble(src);
        if (!bytes_are(len, T[i].b, T[i].n)) {
            fprintf(stderr, "encoding mismatch for '%s' (len %d, err '%s')\n", T[i].src, len, g_err);
            return 1;
        }
    }
    return 0;
}

static int t_forward_reference(void)
{
    static const uint8_t e[] = { 0x21, 0x04, 0x01, 0x76, 0x07 };
    static const uint8_t e2[] = { 0xC3, 0x04, 0x01, 0x00, 0xC3, 0x03, 0x01 };
    ASSERT(bytes_are(assemble("LXI H,DATA\nHLT\nDATA: DB 7\n"), e, 5));
    ASSERT(bytes_are(assemble("        JMP FWD\nBACK:   NOP\nFWD:    JMP BACK\n"), e2, 7));
    return 0;
}

static int t_db_dw_ds(void)
{
    static const uint8_t e1[] = { 0x48, 0x49, 0x00 };
    static const uint8_t e2[] = { 0x34, 0x12 };
    static const uint8_t e3[] = { 0x48, 0x49, 0x00, 0x34, 0x12, 0x00, 0x00, 0xFF, 0x41, 0x42, 0x78, 0x56, 0x01, 0x00 };
    ASSERT(bytes_are(assemble("DB 'HI',0\n"), e1, 3));
    ASSERT(bytes_are(assemble("DW 1234H\n"), e2, 2));
    ASSERT(bytes_are(assemble("DB 'HI',0\nDW 1234H\nDS 2\nDB 0FFH\nDB \"AB\"\nDW 5678H,1\n"), e3, 14));
    return 0;
}

static int t_org_shifts_labels(void)
{
    static const uint8_t e[] = { 0x00, 0x21, 0x00, 0x02, 0xC3, 0x00, 0x02 };
    static const uint8_t d[] = { 0x21, 0x00, 0x01 };
    ASSERT(bytes_are(assemble("ORG 0200H\nL: NOP\nLXI H,L\nJMP L\n"), e, 7));
    ASSERT(bytes_are(assemble("L: LXI H,L\n"), d, 3));      /* default ORG 0100H */
    return 0;
}

static int t_equ_and_dollar(void)
{
    static const uint8_t e[] = { 0x3E, 0x05, 0x21, 0x02, 0x01, 0xC3, 0x05, 0x01, 0x06, 0x06 };
    ASSERT(bytes_are(assemble("X EQU 5\nMVI A,X\nLXI H,$\nJMP $\nY EQU X+1\nMVI B,Y\n"), e, 10));
    return 0;
}

static int t_number_formats_and_expressions(void)
{
    static const uint8_t e1[] = { 0x3E, 0xFF, 0x06, 0xFF, 0x0E, 0xFF, 0x16, 0x0A, 0x1E, 0x41, 0x26, 0x7B };
    static const uint8_t e2[] = { 0x3E, 0x14, 0x06, 0x0E, 0x0E, 0x03, 0x16, 0x1E, 0x1E, 0x06 };
    ASSERT(bytes_are(assemble("MVI A,0FFH\nMVI B,0xFF\nMVI C,$FF\nMVI D,1010B\nMVI E,'A'\nMVI H,123\n"), e1, 12));
    /* expressions evaluate left to right: 2+3*4 = 20, 2+(3*4) = 14, (10-4)/2 = 3, 20/2*3 = 30, 10-2-2 = 6 */
    ASSERT(bytes_are(assemble("MVI A,2+3*4\nMVI B,2+(3*4)\nMVI C,(10-4)/2\nMVI D,20/2*3\nMVI E,10-2-2\n"), e2, 10));
    return 0;
}

static int t_case_comments_end(void)
{
    static const uint8_t e[] = { 0x3E, 0x01, 0x47, 0xF5, 0xC3, 0x00, 0x01 };
    static const uint8_t e2[] = { 0x00 };
    ASSERT(bytes_are(assemble(
        "; a whole-line comment\n"
        "\n"
        "start:  mvi a,1     ; set A\n"
        "        Mov b,A\n"
        "        push psw\n"
        "        jmp start   ; loop\n"
        "        END\n"), e, 7));
    ASSERT(bytes_are(assemble("NOP\nEND\n"), e2, 1));
    return 0;
}

static int t_run_assembled_program(void)
{
    int len = assemble(
        "        ORG 0100H\n"
        "        LXI H,MSG\n"
        "LOOP:   MOV A,M\n"
        "        ORA A\n"
        "        JZ DONE\n"
        "        MOV C,A\n"
        "        MVI A,2         ; CONOUT\n"
        "        OUT 1\n"
        "        INX H\n"
        "        JMP LOOP\n"
        "DONE:   HLT\n"
        "MSG:    DB 'OK',10,0\n");
    ASSERT(len > 0);
    ASSERT(g_bin[0] == 0x21);
    ASSERT(run_prog(NULL, g_bin, (uint32_t)len, NULL) == 0);
    ASSERT(g_steps > 0);
    ASSERT(out_is("OK\n"));
    ASSERT(tos_halt_reason() == TOS_HALT_NONE);
    ASSERT(tos_state() != KS_HALT);
    return 0;
}

/* ---- failure cases ---- */

static int t_unknown_mnemonic(void)
{
    ASSERT(assemble("FOO A,B\n") == -1);
    ASSERT(strcmp(g_err, "line 1: unknown mnemonic 'FOO'") == 0);
    return 0;
}

static int t_unknown_mnemonic_line_number(void)
{
    ASSERT(assemble("NOP\n; comment\nFOO\n") == -1);
    ASSERT(strcmp(g_err, "line 3: unknown mnemonic 'FOO'") == 0);
    return 0;
}

static int t_unknown_mnemonic_after_label(void)
{
    ASSERT(assemble("L1: FOO 5\n") == -1);
    ASSERT(strcmp(g_err, "line 1: unknown mnemonic 'FOO'") == 0);
    return 0;
}

static int t_undefined_symbol_jmp(void)
{
    ASSERT(assemble("JMP NOWHERE\n") == -1);
    ASSERT(strcmp(g_err, "line 1: undefined symbol 'NOWHERE'") == 0);
    return 0;
}

static int t_undefined_symbol_case_sensitive_label(void)
{
    ASSERT(assemble("loop: NOP\nJMP LOOP\n") == -1);
    ASSERT(strcmp(g_err, "line 2: undefined symbol 'LOOP'") == 0);
    return 0;
}

static int t_undefined_symbol_dw(void)
{
    ASSERT(assemble("NOP\nDW UNDEF\n") == -1);
    ASSERT(strcmp(g_err, "line 2: undefined symbol 'UNDEF'") == 0);
    return 0;
}

static int t_bad_operand_missing(void)
{
    ASSERT(assemble("MOV A\n") == -1);
    ASSERT(strcmp(g_err, "line 1: bad operand") == 0);
    return 0;
}

static int t_bad_operand_jmp_without_target(void)
{
    ASSERT(assemble("NOP\nJMP\n") == -1);
    ASSERT(strcmp(g_err, "line 2: bad operand") == 0);
    return 0;
}

static int t_value_out_of_range_mvi(void)
{
    ASSERT(assemble("MVI A,256\n") == -1);
    ASSERT(strcmp(g_err, "line 1: value out of range") == 0);
    return 0;
}

static int t_value_out_of_range_db(void)
{
    ASSERT(assemble("NOP\nNOP\nDB 1,300\n") == -1);
    ASSERT(strcmp(g_err, "line 3: value out of range") == 0);
    return 0;
}

static int t_value_out_of_range_lxi(void)
{
    ASSERT(assemble("LXI H,70000\n") == -1);
    ASSERT(strcmp(g_err, "line 1: value out of range") == 0);
    return 0;
}

int main(void)
{
    TEST("WS7-01: MVI A,05H -> 3E 05; RST 3 -> DF", t_mvi_rst_basic);
    TEST("WS7-01: every 8080 mnemonic encodes per Intel's table", t_opcode_table);
    TEST("WS7-01: LXI H,LABEL forward reference resolves (two passes)", t_forward_reference);
    TEST("WS7-01: DB 'HI',0 -> 48 49 00; DW 1234H -> 34 12; DS zero-fills", t_db_dw_ds);
    TEST("WS7-01: ORG 0200H shifts label values; default ORG is 0100H", t_org_shifts_labels);
    TEST("WS7-01: EQU and $ work", t_equ_and_dollar);
    TEST("WS7-01: 0FFH/0xFF/$FF/1010B/'A' numbers; expressions left-to-right", t_number_formats_and_expressions);
    TEST("WS7-01: mnemonics/registers case-insensitive, comments, END", t_case_comments_end);
    TEST("WS7-01: an assembled CONOUT loop prints OK through the API", t_run_assembled_program);
    TEST("WS7-01: unknown mnemonic 'FOO' (failure)", t_unknown_mnemonic);
    TEST("WS7-01: unknown mnemonic reports line 3 (failure)", t_unknown_mnemonic_line_number);
    TEST("WS7-01: unknown mnemonic after a label (failure)", t_unknown_mnemonic_after_label);
    TEST("WS7-01: JMP NOWHERE -> undefined symbol (failure)", t_undefined_symbol_jmp);
    TEST("WS7-01: labels are case-sensitive -> undefined symbol 'LOOP' (failure)", t_undefined_symbol_case_sensitive_label);
    TEST("WS7-01: DW UNDEF -> undefined symbol at line 2 (failure)", t_undefined_symbol_dw);
    TEST("WS7-01: MOV A -> bad operand (failure)", t_bad_operand_missing);
    TEST("WS7-01: JMP with no target -> bad operand at line 2 (failure)", t_bad_operand_jmp_without_target);
    TEST("WS7-01: MVI A,256 -> value out of range (failure)", t_value_out_of_range_mvi);
    TEST("WS7-01: DB 300 -> value out of range at line 3 (failure)", t_value_out_of_range_db);
    TEST("WS7-01: LXI H,70000 -> value out of range (failure)", t_value_out_of_range_lxi);
    printf("PASS: test_v2_ws7_01_asm\n");
    RUN_ALL_TESTS();
}
