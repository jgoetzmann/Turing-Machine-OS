/* src/emu/disasm.c — 8080 disassembler for TuringOS v2.
 *
 * One 256-entry table of mnemonic templates. Inside a template:
 *   '#'  -> 8-bit immediate  (bytes[1])            printed as two hex digits + "H"
 *   '@'  -> 16-bit immediate (bytes[1] | bytes[2]<<8) printed as four hex digits + "H"
 * Everything else is copied verbatim. Instruction length is derived from
 * the template ('@' = 3, '#' = 2, neither = 1). Undocumented aliases carry
 * a trailing '*' on the mnemonic: NOP*, JMP*, RET*, CALL*. Where several bytes share one
 * alias, the byte is part of the spelling (NOP*10 ... NOP*38, CALL*ED, CALL*FD), so a
 * listing re-assembles to the bytes it came from.
 */
#include "disasm.h"

static const char *const k_tmpl[256] = {
    /* 00 */ "NOP",      "LXI B,@",  "STAX B",   "INX B",    "INR B",    "DCR B",    "MVI B,#",  "RLC",
    /* 08 */ "NOP*",     "DAD B",    "LDAX B",   "DCX B",    "INR C",    "DCR C",    "MVI C,#",  "RRC",
    /* 10 */ "NOP*10",   "LXI D,@",  "STAX D",   "INX D",    "INR D",    "DCR D",    "MVI D,#",  "RAL",
    /* 18 */ "NOP*18",   "DAD D",    "LDAX D",   "DCX D",    "INR E",    "DCR E",    "MVI E,#",  "RAR",
    /* 20 */ "NOP*20",   "LXI H,@",  "SHLD @",   "INX H",    "INR H",    "DCR H",    "MVI H,#",  "DAA",
    /* 28 */ "NOP*28",   "DAD H",    "LHLD @",   "DCX H",    "INR L",    "DCR L",    "MVI L,#",  "CMA",
    /* 30 */ "NOP*30",   "LXI SP,@", "STA @",    "INX SP",   "INR M",    "DCR M",    "MVI M,#",  "STC",
    /* 38 */ "NOP*38",   "DAD SP",   "LDA @",    "DCX SP",   "INR A",    "DCR A",    "MVI A,#",  "CMC",
    /* 40 */ "MOV B,B",  "MOV B,C",  "MOV B,D",  "MOV B,E",  "MOV B,H",  "MOV B,L",  "MOV B,M",  "MOV B,A",
    /* 48 */ "MOV C,B",  "MOV C,C",  "MOV C,D",  "MOV C,E",  "MOV C,H",  "MOV C,L",  "MOV C,M",  "MOV C,A",
    /* 50 */ "MOV D,B",  "MOV D,C",  "MOV D,D",  "MOV D,E",  "MOV D,H",  "MOV D,L",  "MOV D,M",  "MOV D,A",
    /* 58 */ "MOV E,B",  "MOV E,C",  "MOV E,D",  "MOV E,E",  "MOV E,H",  "MOV E,L",  "MOV E,M",  "MOV E,A",
    /* 60 */ "MOV H,B",  "MOV H,C",  "MOV H,D",  "MOV H,E",  "MOV H,H",  "MOV H,L",  "MOV H,M",  "MOV H,A",
    /* 68 */ "MOV L,B",  "MOV L,C",  "MOV L,D",  "MOV L,E",  "MOV L,H",  "MOV L,L",  "MOV L,M",  "MOV L,A",
    /* 70 */ "MOV M,B",  "MOV M,C",  "MOV M,D",  "MOV M,E",  "MOV M,H",  "MOV M,L",  "HLT",      "MOV M,A",
    /* 78 */ "MOV A,B",  "MOV A,C",  "MOV A,D",  "MOV A,E",  "MOV A,H",  "MOV A,L",  "MOV A,M",  "MOV A,A",
    /* 80 */ "ADD B",    "ADD C",    "ADD D",    "ADD E",    "ADD H",    "ADD L",    "ADD M",    "ADD A",
    /* 88 */ "ADC B",    "ADC C",    "ADC D",    "ADC E",    "ADC H",    "ADC L",    "ADC M",    "ADC A",
    /* 90 */ "SUB B",    "SUB C",    "SUB D",    "SUB E",    "SUB H",    "SUB L",    "SUB M",    "SUB A",
    /* 98 */ "SBB B",    "SBB C",    "SBB D",    "SBB E",    "SBB H",    "SBB L",    "SBB M",    "SBB A",
    /* A0 */ "ANA B",    "ANA C",    "ANA D",    "ANA E",    "ANA H",    "ANA L",    "ANA M",    "ANA A",
    /* A8 */ "XRA B",    "XRA C",    "XRA D",    "XRA E",    "XRA H",    "XRA L",    "XRA M",    "XRA A",
    /* B0 */ "ORA B",    "ORA C",    "ORA D",    "ORA E",    "ORA H",    "ORA L",    "ORA M",    "ORA A",
    /* B8 */ "CMP B",    "CMP C",    "CMP D",    "CMP E",    "CMP H",    "CMP L",    "CMP M",    "CMP A",
    /* C0 */ "RNZ",      "POP B",    "JNZ @",    "JMP @",    "CNZ @",    "PUSH B",   "ADI #",    "RST 0",
    /* C8 */ "RZ",       "RET",      "JZ @",     "JMP* @",   "CZ @",     "CALL @",   "ACI #",    "RST 1",
    /* D0 */ "RNC",      "POP D",    "JNC @",    "OUT #",    "CNC @",    "PUSH D",   "SUI #",    "RST 2",
    /* D8 */ "RC",       "RET*",     "JC @",     "IN #",     "CC @",     "CALL* @",  "SBI #",    "RST 3",
    /* E0 */ "RPO",      "POP H",    "JPO @",    "XTHL",     "CPO @",    "PUSH H",   "ANI #",    "RST 4",
    /* E8 */ "RPE",      "PCHL",     "JPE @",    "XCHG",     "CPE @",    "CALL*ED @",  "XRI #",    "RST 5",
    /* F0 */ "RP",       "POP PSW",  "JP @",     "DI",       "CP @",     "PUSH PSW", "ORI #",    "RST 6",
    /* F8 */ "RM",       "SPHL",     "JM @",     "EI",       "CM @",     "CALL*FD @",  "CPI #",    "RST 7"
};

static const char k_hex[16] = {
    '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
};

/* Bounded writer: never writes past out[cap-1]; always leaves room for NUL. */
typedef struct {
    char *out;
    int   cap;
    int   n;
} dwriter_t;

static void dput(dwriter_t *w, char ch) {
    if (w->out != 0 && w->n < w->cap - 1) {
        w->out[w->n] = ch;
    }
    w->n++;
}

static void dput_hex8(dwriter_t *w, uint8_t v) {
    dput(w, k_hex[(v >> 4u) & 0x0Fu]);
    dput(w, k_hex[v & 0x0Fu]);
}

static void dput_hex16(dwriter_t *w, uint16_t v) {
    dput_hex8(w, (uint8_t)(v >> 8u));
    dput_hex8(w, (uint8_t)(v & 0xFFu));
}

int disasm_one(const uint8_t *bytes, uint16_t addr, char *out, int cap) {
    const char *t;
    dwriter_t w;
    int len = 1;

    (void)addr;   /* the output has no address prefix; callers print it themselves */

    w.out = out;
    w.cap = cap;
    w.n   = 0;

    if (bytes == 0) {
        if (out != 0 && cap > 0) {
            out[0] = '\0';
        }
        return 1;
    }

    for (t = k_tmpl[bytes[0]]; *t != '\0'; t++) {
        if (*t == '#') {
            dput_hex8(&w, bytes[1]);
            dput(&w, 'H');
            len = 2;
        } else if (*t == '@') {
            dput_hex16(&w, (uint16_t)(bytes[1] | ((uint16_t)bytes[2] << 8u)));
            dput(&w, 'H');
            len = 3;
        } else {
            dput(&w, *t);
        }
    }

    if (out != 0 && cap > 0) {
        out[(w.n < cap - 1) ? w.n : (cap - 1)] = '\0';
    }
    return len;
}
