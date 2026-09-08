/* src/emu/cpu.c — Intel 8080 core for TuringOS v2.
 *
 * Every one of the 256 opcodes is decoded. The undocumented aliases behave
 * as their documented twins:
 *   0x08,0x10,0x18,0x28,0x38 -> NOP ; 0xCB -> JMP ; 0xD9 -> RET ;
 *   0xDD,0xED,0xFD -> CALL.  0x20 = RIM, 0x30 = SIM (8085 flag storage only).
 *
 * Memory goes through mem_read/mem_write (src/emu/mem.c). I/O: IN reads
 * cpu->io_in_ports[port]; OUT latches port/value into io_out_* and raises
 * io_out_pending for the kernel to service. Cycles follow Intel's table.
 */
#include "cpu.h"
#include "mem.h"
#include "../tos.h"

#include <stddef.h>
#include <string.h>

#define FLAG_S  0x80u
#define FLAG_Z  0x40u
#define FLAG_AC 0x10u
#define FLAG_P  0x04u
#define FLAG_CY 0x01u

/* ---- opcode tables ---------------------------------------------------- */

/* Instruction length in bytes, indexed by opcode. */
static const uint8_t k_len[256] = {
    /* 00 */ 1,3,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    /* 10 */ 1,3,1,1,1,1,2,1, 1,1,1,1,1,1,2,1,
    /* 20 */ 1,3,3,1,1,1,2,1, 1,1,3,1,1,1,2,1,
    /* 30 */ 1,3,3,1,1,1,2,1, 1,1,3,1,1,1,2,1,
    /* 40 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* 50 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* 60 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* 70 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* 80 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* 90 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* A0 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* B0 */ 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    /* C0 */ 1,1,3,3,3,1,2,1, 1,1,3,3,3,3,2,1,
    /* D0 */ 1,1,3,2,3,1,2,1, 1,1,3,2,3,3,2,1,
    /* E0 */ 1,1,3,1,3,1,2,1, 1,1,3,1,3,3,2,1,
    /* F0 */ 1,1,3,1,3,1,2,1, 1,1,3,1,3,3,2,1
};

/* Intel 8080 cycle counts. Conditional CALL/RET hold the not-taken value
 * (11 / 5); cpu_opcode_cycles() substitutes 17 / 11 when taken. */
static const uint8_t k_cycles[256] = {
    /* 00 */  4,10, 7, 5, 5, 5, 7, 4,  4,10, 7, 5, 5, 5, 7, 4,
    /* 10 */  4,10, 7, 5, 5, 5, 7, 4,  4,10, 7, 5, 5, 5, 7, 4,
    /* 20 */  4,10,16, 5, 5, 5, 7, 4,  4,10,16, 5, 5, 5, 7, 4,
    /* 30 */  4,10,13, 5,10,10,10, 4,  4,10,13, 5, 5, 5, 7, 4,
    /* 40 */  5, 5, 5, 5, 5, 5, 7, 5,  5, 5, 5, 5, 5, 5, 7, 5,
    /* 50 */  5, 5, 5, 5, 5, 5, 7, 5,  5, 5, 5, 5, 5, 5, 7, 5,
    /* 60 */  5, 5, 5, 5, 5, 5, 7, 5,  5, 5, 5, 5, 5, 5, 7, 5,
    /* 70 */  7, 7, 7, 7, 7, 7, 7, 7,  5, 5, 5, 5, 5, 5, 7, 5,
    /* 80 */  4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    /* 90 */  4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    /* A0 */  4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    /* B0 */  4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    /* C0 */  5,10,10,10,11,11, 7,11,  5,10,10,10,11,17, 7,11,
    /* D0 */  5,10,10,10,11,11, 7,11,  5,10,10,10,11,17, 7,11,
    /* E0 */  5,10,10,18,11,11, 7,11,  5, 5,10, 4,11,17, 7,11,
    /* F0 */  5,10,10, 4,11,11, 7,11,  5, 5,10, 4,11,17, 7,11
};

uint8_t cpu_opcode_len(uint8_t opcode) {
    return k_len[opcode];
}

uint8_t cpu_opcode_cycles(uint8_t opcode, int taken) {
    if ((opcode & 0xC7u) == 0xC4u) {           /* Ccc */
        return taken ? 17u : 11u;
    }
    if ((opcode & 0xC7u) == 0xC0u) {           /* Rcc */
        return taken ? 11u : 5u;
    }
    return k_cycles[opcode];
}

/* ---- register helpers ------------------------------------------------- */

static uint16_t get_hl(const cpu_t *cpu) {
    return (uint16_t)(((uint16_t)cpu->h << 8u) | cpu->l);
}

static uint16_t get_rp(const cpu_t *cpu, uint8_t rp) {
    switch (rp & 0x03u) {
        case 0x00u: return (uint16_t)(((uint16_t)cpu->b << 8u) | cpu->c);
        case 0x01u: return (uint16_t)(((uint16_t)cpu->d << 8u) | cpu->e);
        case 0x02u: return get_hl(cpu);
        default:    return cpu->sp;
    }
}

static void set_rp(cpu_t *cpu, uint8_t rp, uint16_t value) {
    switch (rp & 0x03u) {
        case 0x00u:
            cpu->b = (uint8_t)(value >> 8u);
            cpu->c = (uint8_t)(value & 0xFFu);
            break;
        case 0x01u:
            cpu->d = (uint8_t)(value >> 8u);
            cpu->e = (uint8_t)(value & 0xFFu);
            break;
        case 0x02u:
            cpu->h = (uint8_t)(value >> 8u);
            cpu->l = (uint8_t)(value & 0xFFu);
            break;
        default:
            cpu->sp = value;
            break;
    }
}

/* reg code 0..7 = B C D E H L M A */
static uint8_t read_reg(const cpu_t *cpu, uint8_t code) {
    switch (code & 0x07u) {
        case 0x00u: return cpu->b;
        case 0x01u: return cpu->c;
        case 0x02u: return cpu->d;
        case 0x03u: return cpu->e;
        case 0x04u: return cpu->h;
        case 0x05u: return cpu->l;
        case 0x06u: return mem_read(get_hl(cpu));
        default:    return cpu->a;
    }
}

static void write_reg(cpu_t *cpu, uint8_t code, uint8_t value) {
    switch (code & 0x07u) {
        case 0x00u: cpu->b = value; break;
        case 0x01u: cpu->c = value; break;
        case 0x02u: cpu->d = value; break;
        case 0x03u: cpu->e = value; break;
        case 0x04u: cpu->h = value; break;
        case 0x05u: cpu->l = value; break;
        case 0x06u: mem_write(get_hl(cpu), value); break;
        default:    cpu->a = value; break;
    }
}

/* ---- fetch / stack ---------------------------------------------------- */

static uint8_t fetch8(cpu_t *cpu) {
    const uint8_t v = mem_read(cpu->pc);
    cpu->pc = (uint16_t)(cpu->pc + 1u);
    return v;
}

static uint16_t fetch16(cpu_t *cpu) {
    const uint8_t lo = fetch8(cpu);
    const uint8_t hi = fetch8(cpu);
    return (uint16_t)(lo | ((uint16_t)hi << 8u));
}

static void push16(cpu_t *cpu, uint16_t value) {
    mem_write((uint16_t)(cpu->sp - 1u), (uint8_t)(value >> 8u));
    mem_write((uint16_t)(cpu->sp - 2u), (uint8_t)(value & 0xFFu));
    cpu->sp = (uint16_t)(cpu->sp - 2u);
}

static uint16_t pop16(cpu_t *cpu) {
    const uint8_t lo = mem_read(cpu->sp);
    const uint8_t hi = mem_read((uint16_t)(cpu->sp + 1u));
    cpu->sp = (uint16_t)(cpu->sp + 2u);
    return (uint16_t)(lo | ((uint16_t)hi << 8u));
}

/* ---- flags ------------------------------------------------------------ */

static int cond_true(const cpu_t *cpu, uint8_t cc) {
    switch (cc & 0x07u) {
        case 0x00u: return (cpu->flags & FLAG_Z)  == 0u;  /* NZ */
        case 0x01u: return (cpu->flags & FLAG_Z)  != 0u;  /* Z  */
        case 0x02u: return (cpu->flags & FLAG_CY) == 0u;  /* NC */
        case 0x03u: return (cpu->flags & FLAG_CY) != 0u;  /* C  */
        case 0x04u: return (cpu->flags & FLAG_P)  == 0u;  /* PO */
        case 0x05u: return (cpu->flags & FLAG_P)  != 0u;  /* PE */
        case 0x06u: return (cpu->flags & FLAG_S)  == 0u;  /* P  */
        default:    return (cpu->flags & FLAG_S)  != 0u;  /* M  */
    }
}

static uint8_t pack_psw(uint8_t flags) {
    /* bit 1 always reads as 1, bits 3 and 5 as 0 */
    return (uint8_t)((flags & (FLAG_S | FLAG_Z | FLAG_AC | FLAG_P | FLAG_CY)) | 0x02u);
}

static uint8_t unpack_psw(uint8_t psw) {
    return (uint8_t)(psw & (FLAG_S | FLAG_Z | FLAG_AC | FLAG_P | FLAG_CY));
}

static uint8_t parity_even(uint8_t v) {
    v ^= (uint8_t)(v >> 4u);
    v ^= (uint8_t)(v >> 2u);
    v ^= (uint8_t)(v >> 1u);
    return (uint8_t)((v & 1u) == 0u);
}

static void set_szp(cpu_t *cpu, uint8_t result) {
    cpu->flags &= (uint8_t)~(FLAG_S | FLAG_Z | FLAG_P);
    if ((result & 0x80u) != 0u) cpu->flags |= FLAG_S;
    if (result == 0u)           cpu->flags |= FLAG_Z;
    if (parity_even(result))    cpu->flags |= FLAG_P;
}

static void set_cy_ac(cpu_t *cpu, int cy, int ac) {
    cpu->flags &= (uint8_t)~(FLAG_CY | FLAG_AC);
    if (cy) cpu->flags |= FLAG_CY;
    if (ac) cpu->flags |= FLAG_AC;
}

/* ---- ALU -------------------------------------------------------------- */

static void alu_add(cpu_t *cpu, uint8_t v, uint8_t carry_in) {
    const uint16_t sum = (uint16_t)cpu->a + (uint16_t)v + (uint16_t)carry_in;
    const int ac = ((cpu->a & 0x0Fu) + (v & 0x0Fu) + carry_in) > 0x0Fu;
    cpu->a = (uint8_t)(sum & 0xFFu);
    set_szp(cpu, cpu->a);
    set_cy_ac(cpu, sum > 0xFFu, ac);
}

static void alu_sub(cpu_t *cpu, uint8_t v, uint8_t borrow_in) {
    const uint16_t sub  = (uint16_t)v + (uint16_t)borrow_in;
    const uint16_t diff = (uint16_t)cpu->a - sub;
    const int cy = (uint16_t)cpu->a < sub;
    const int ac = (cpu->a & 0x0Fu) < ((v & 0x0Fu) + borrow_in);
    cpu->a = (uint8_t)(diff & 0xFFu);
    set_szp(cpu, cpu->a);
    set_cy_ac(cpu, cy, ac);
}

static void alu_cmp(cpu_t *cpu, uint8_t v) {
    const uint8_t a = cpu->a;
    const uint8_t result = (uint8_t)(a - v);
    set_szp(cpu, result);
    set_cy_ac(cpu, a < v, (a & 0x0Fu) < (v & 0x0Fu));
}

static void alu_ana(cpu_t *cpu, uint8_t v) {
    cpu->a = (uint8_t)(cpu->a & v);
    set_szp(cpu, cpu->a);
    set_cy_ac(cpu, 0, 1);            /* 8080 ANA/ANI: CY cleared, AC set */
}

static void alu_xra(cpu_t *cpu, uint8_t v) {
    cpu->a = (uint8_t)(cpu->a ^ v);
    set_szp(cpu, cpu->a);
    set_cy_ac(cpu, 0, 0);
}

static void alu_ora(cpu_t *cpu, uint8_t v) {
    cpu->a = (uint8_t)(cpu->a | v);
    set_szp(cpu, cpu->a);
    set_cy_ac(cpu, 0, 0);
}

static void alu_inr(cpu_t *cpu, uint8_t code) {
    const uint8_t before = read_reg(cpu, code);
    const uint8_t result = (uint8_t)(before + 1u);
    write_reg(cpu, code, result);
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~FLAG_AC;
    if ((before & 0x0Fu) == 0x0Fu) cpu->flags |= FLAG_AC;
}

static void alu_dcr(cpu_t *cpu, uint8_t code) {
    const uint8_t before = read_reg(cpu, code);
    const uint8_t result = (uint8_t)(before - 1u);
    write_reg(cpu, code, result);
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~FLAG_AC;
    if ((before & 0x0Fu) == 0x00u) cpu->flags |= FLAG_AC;
}

static void alu_dad(cpu_t *cpu, uint8_t rp) {
    const uint32_t sum = (uint32_t)get_hl(cpu) + (uint32_t)get_rp(cpu, rp);
    cpu->h = (uint8_t)((sum >> 8u) & 0xFFu);
    cpu->l = (uint8_t)(sum & 0xFFu);
    cpu->flags &= (uint8_t)~FLAG_CY;
    if ((sum & 0x10000u) != 0u) cpu->flags |= FLAG_CY;
}

static void alu_daa(cpu_t *cpu) {
    uint8_t adjust = 0u;
    int new_cy = (cpu->flags & FLAG_CY) != 0u;
    if (((cpu->a & 0x0Fu) > 9u) || ((cpu->flags & FLAG_AC) != 0u)) {
        adjust |= 0x06u;
    }
    if ((cpu->a > 0x99u) || ((cpu->flags & FLAG_CY) != 0u)) {
        adjust |= 0x60u;
        new_cy = 1;
    }
    {
        const int ac = ((cpu->a & 0x0Fu) + (adjust & 0x0Fu)) > 0x0Fu;
        cpu->a = (uint8_t)(cpu->a + adjust);
        set_szp(cpu, cpu->a);
        set_cy_ac(cpu, new_cy, ac);
    }
}

static void set_cy(cpu_t *cpu, int cy) {
    cpu->flags &= (uint8_t)~FLAG_CY;
    if (cy) cpu->flags |= FLAG_CY;
}

/* ---- lifecycle -------------------------------------------------------- */

void cpu_init(cpu_t *cpu) {
    if (cpu == NULL) {
        return;
    }
    memset(cpu, 0, sizeof(*cpu));
    cpu->sp = (uint16_t)TOS_STACK_TOP(TOS_TAPE_LEN_64K);   /* 0xFDFF; the kernel sets the real SP */
}

void cpu_reset(cpu_t *cpu) {
    cpu_init(cpu);
}

int cpu_halted(const cpu_t *cpu) {
    if (cpu == NULL) {
        return 1;
    }
    return cpu->halted;
}

/* ---- execute ---------------------------------------------------------- */

void cpu_step(cpu_t *cpu) {
    uint8_t op;
    int taken = 0;

    if (cpu == NULL || cpu->halted) {
        return;
    }

    op = fetch8(cpu);

    if (op >= 0x40u && op <= 0x7Fu && op != 0x76u) {
        /* MOV dst,src */
        write_reg(cpu, (uint8_t)((op >> 3u) & 0x07u), read_reg(cpu, (uint8_t)(op & 0x07u)));
    } else if (op >= 0x80u && op <= 0xBFu) {
        /* ADD ADC SUB SBB ANA XRA ORA CMP  r */
        const uint8_t v = read_reg(cpu, (uint8_t)(op & 0x07u));
        const uint8_t cy = (uint8_t)((cpu->flags & FLAG_CY) != 0u);
        switch ((op >> 3u) & 0x07u) {
            case 0x00u: alu_add(cpu, v, 0u); break;
            case 0x01u: alu_add(cpu, v, cy); break;
            case 0x02u: alu_sub(cpu, v, 0u); break;
            case 0x03u: alu_sub(cpu, v, cy); break;
            case 0x04u: alu_ana(cpu, v);     break;
            case 0x05u: alu_xra(cpu, v);     break;
            case 0x06u: alu_ora(cpu, v);     break;
            default:    alu_cmp(cpu, v);     break;
        }
    } else {
        switch (op) {
            /* ---- 0x00..0x3F ---- */
            case 0x00u: case 0x08u: case 0x10u: case 0x18u:
            case 0x28u: case 0x38u:                              /* NOP, NOP* */
                break;
            case 0x20u:                                          /* RIM */
                cpu->a = cpu->rim_value;
                break;
            case 0x30u:                                          /* SIM */
                cpu->sim_value = cpu->a;
                break;

            case 0x01u: case 0x11u: case 0x21u: case 0x31u:      /* LXI rp,d16 */
                set_rp(cpu, (uint8_t)((op >> 4u) & 0x03u), fetch16(cpu));
                break;

            case 0x02u:                                          /* STAX B */
                mem_write(get_rp(cpu, 0u), cpu->a);
                break;
            case 0x12u:                                          /* STAX D */
                mem_write(get_rp(cpu, 1u), cpu->a);
                break;
            case 0x0Au:                                          /* LDAX B */
                cpu->a = mem_read(get_rp(cpu, 0u));
                break;
            case 0x1Au:                                          /* LDAX D */
                cpu->a = mem_read(get_rp(cpu, 1u));
                break;

            case 0x22u: {                                        /* SHLD a16 */
                const uint16_t a = fetch16(cpu);
                mem_write(a, cpu->l);
                mem_write((uint16_t)(a + 1u), cpu->h);
                break;
            }
            case 0x2Au: {                                        /* LHLD a16 */
                const uint16_t a = fetch16(cpu);
                cpu->l = mem_read(a);
                cpu->h = mem_read((uint16_t)(a + 1u));
                break;
            }
            case 0x32u: {                                        /* STA a16 */
                const uint16_t a = fetch16(cpu);
                mem_write(a, cpu->a);
                break;
            }
            case 0x3Au: {                                        /* LDA a16 */
                const uint16_t a = fetch16(cpu);
                cpu->a = mem_read(a);
                break;
            }

            case 0x03u: case 0x13u: case 0x23u: case 0x33u: {    /* INX rp */
                const uint8_t rp = (uint8_t)((op >> 4u) & 0x03u);
                set_rp(cpu, rp, (uint16_t)(get_rp(cpu, rp) + 1u));
                break;
            }
            case 0x0Bu: case 0x1Bu: case 0x2Bu: case 0x3Bu: {    /* DCX rp */
                const uint8_t rp = (uint8_t)((op >> 4u) & 0x03u);
                set_rp(cpu, rp, (uint16_t)(get_rp(cpu, rp) - 1u));
                break;
            }
            case 0x09u: case 0x19u: case 0x29u: case 0x39u:      /* DAD rp */
                alu_dad(cpu, (uint8_t)((op >> 4u) & 0x03u));
                break;

            case 0x04u: case 0x0Cu: case 0x14u: case 0x1Cu:
            case 0x24u: case 0x2Cu: case 0x34u: case 0x3Cu:      /* INR r */
                alu_inr(cpu, (uint8_t)((op >> 3u) & 0x07u));
                break;
            case 0x05u: case 0x0Du: case 0x15u: case 0x1Du:
            case 0x25u: case 0x2Du: case 0x35u: case 0x3Du:      /* DCR r */
                alu_dcr(cpu, (uint8_t)((op >> 3u) & 0x07u));
                break;
            case 0x06u: case 0x0Eu: case 0x16u: case 0x1Eu:
            case 0x26u: case 0x2Eu: case 0x36u: case 0x3Eu: {    /* MVI r,d8 */
                const uint8_t v = fetch8(cpu);
                write_reg(cpu, (uint8_t)((op >> 3u) & 0x07u), v);
                break;
            }

            case 0x07u: {                                        /* RLC */
                const uint8_t hi = (uint8_t)((cpu->a >> 7u) & 1u);
                cpu->a = (uint8_t)((cpu->a << 1u) | hi);
                set_cy(cpu, hi);
                break;
            }
            case 0x0Fu: {                                        /* RRC */
                const uint8_t lo = (uint8_t)(cpu->a & 1u);
                cpu->a = (uint8_t)((cpu->a >> 1u) | (uint8_t)(lo << 7u));
                set_cy(cpu, lo);
                break;
            }
            case 0x17u: {                                        /* RAL */
                const uint8_t old_cy = (uint8_t)((cpu->flags & FLAG_CY) != 0u);
                const uint8_t hi = (uint8_t)((cpu->a >> 7u) & 1u);
                cpu->a = (uint8_t)((cpu->a << 1u) | old_cy);
                set_cy(cpu, hi);
                break;
            }
            case 0x1Fu: {                                        /* RAR */
                const uint8_t old_cy = (uint8_t)((cpu->flags & FLAG_CY) != 0u);
                const uint8_t lo = (uint8_t)(cpu->a & 1u);
                cpu->a = (uint8_t)((cpu->a >> 1u) | (uint8_t)(old_cy << 7u));
                set_cy(cpu, lo);
                break;
            }
            case 0x27u:                                          /* DAA */
                alu_daa(cpu);
                break;
            case 0x2Fu:                                          /* CMA */
                cpu->a = (uint8_t)~cpu->a;
                break;
            case 0x37u:                                          /* STC */
                cpu->flags |= FLAG_CY;
                break;
            case 0x3Fu:                                          /* CMC */
                cpu->flags ^= FLAG_CY;
                break;

            /* ---- 0x76 ---- */
            case 0x76u:                                          /* HLT */
                cpu->halted = 1;
                break;

            /* ---- 0xC0..0xFF ---- */
            case 0xC0u: case 0xC8u: case 0xD0u: case 0xD8u:
            case 0xE0u: case 0xE8u: case 0xF0u: case 0xF8u:      /* Rcc */
                if (cond_true(cpu, (uint8_t)((op >> 3u) & 0x07u))) {
                    cpu->pc = pop16(cpu);
                    taken = 1;
                }
                break;

            case 0xC1u: case 0xD1u: case 0xE1u:                  /* POP B/D/H */
                set_rp(cpu, (uint8_t)((op >> 4u) & 0x03u), pop16(cpu));
                break;
            case 0xF1u: {                                        /* POP PSW */
                const uint16_t v = pop16(cpu);
                cpu->a = (uint8_t)(v >> 8u);
                cpu->flags = unpack_psw((uint8_t)(v & 0xFFu));
                break;
            }

            case 0xC2u: case 0xCAu: case 0xD2u: case 0xDAu:
            case 0xE2u: case 0xEAu: case 0xF2u: case 0xFAu: {    /* Jcc a16 */
                const uint16_t target = fetch16(cpu);
                if (cond_true(cpu, (uint8_t)((op >> 3u) & 0x07u))) {
                    cpu->pc = target;
                    taken = 1;
                }
                break;
            }
            case 0xC3u: case 0xCBu:                              /* JMP, JMP* */
                cpu->pc = fetch16(cpu);
                break;

            case 0xC4u: case 0xCCu: case 0xD4u: case 0xDCu:
            case 0xE4u: case 0xECu: case 0xF4u: case 0xFCu: {    /* Ccc a16 */
                const uint16_t target = fetch16(cpu);
                if (cond_true(cpu, (uint8_t)((op >> 3u) & 0x07u))) {
                    push16(cpu, cpu->pc);
                    cpu->pc = target;
                    taken = 1;
                }
                break;
            }
            case 0xCDu: case 0xDDu: case 0xEDu: case 0xFDu: {    /* CALL, CALL* */
                const uint16_t target = fetch16(cpu);
                push16(cpu, cpu->pc);
                cpu->pc = target;
                break;
            }

            case 0xC5u: case 0xD5u: case 0xE5u:                  /* PUSH B/D/H */
                push16(cpu, get_rp(cpu, (uint8_t)((op >> 4u) & 0x03u)));
                break;
            case 0xF5u:                                          /* PUSH PSW */
                push16(cpu, (uint16_t)(((uint16_t)cpu->a << 8u) | pack_psw(cpu->flags)));
                break;

            case 0xC6u:                                          /* ADI d8 */
                alu_add(cpu, fetch8(cpu), 0u);
                break;
            case 0xCEu:                                          /* ACI d8 */
                alu_add(cpu, fetch8(cpu), (uint8_t)((cpu->flags & FLAG_CY) != 0u));
                break;
            case 0xD6u:                                          /* SUI d8 */
                alu_sub(cpu, fetch8(cpu), 0u);
                break;
            case 0xDEu:                                          /* SBI d8 */
                alu_sub(cpu, fetch8(cpu), (uint8_t)((cpu->flags & FLAG_CY) != 0u));
                break;
            case 0xE6u:                                          /* ANI d8 */
                alu_ana(cpu, fetch8(cpu));
                break;
            case 0xEEu:                                          /* XRI d8 */
                alu_xra(cpu, fetch8(cpu));
                break;
            case 0xF6u:                                          /* ORI d8 */
                alu_ora(cpu, fetch8(cpu));
                break;
            case 0xFEu:                                          /* CPI d8 */
                alu_cmp(cpu, fetch8(cpu));
                break;

            case 0xC7u: case 0xCFu: case 0xD7u: case 0xDFu:
            case 0xE7u: case 0xEFu: case 0xF7u: case 0xFFu:      /* RST n */
                push16(cpu, cpu->pc);
                cpu->pc = (uint16_t)(((op >> 3u) & 0x07u) * 8u);
                break;

            case 0xC9u: case 0xD9u:                              /* RET, RET* */
                cpu->pc = pop16(cpu);
                break;

            case 0xD3u: {                                        /* OUT p8 */
                const uint8_t port = fetch8(cpu);
                cpu->io_out_pending = 1u;
                cpu->io_out_port = port;
                cpu->io_out_value = cpu->a;
                break;
            }
            case 0xDBu: {                                        /* IN p8 */
                const uint8_t port = fetch8(cpu);
                cpu->a = cpu->io_in_ports[port];
                break;
            }

            case 0xE3u: {                                        /* XTHL */
                const uint8_t lo = mem_read(cpu->sp);
                const uint8_t hi = mem_read((uint16_t)(cpu->sp + 1u));
                mem_write(cpu->sp, cpu->l);
                mem_write((uint16_t)(cpu->sp + 1u), cpu->h);
                cpu->l = lo;
                cpu->h = hi;
                break;
            }
            case 0xE9u:                                          /* PCHL */
                cpu->pc = get_hl(cpu);
                break;
            case 0xEBu: {                                        /* XCHG */
                const uint8_t d = cpu->d;
                const uint8_t e = cpu->e;
                cpu->d = cpu->h;
                cpu->e = cpu->l;
                cpu->h = d;
                cpu->l = e;
                break;
            }
            case 0xF3u:                                          /* DI */
                cpu->interrupts_enabled = 0u;
                break;
            case 0xFBu:                                          /* EI */
                cpu->interrupts_enabled = 1u;
                break;
            case 0xF9u:                                          /* SPHL */
                cpu->sp = get_hl(cpu);
                break;

            default:
                /* Unreachable: every opcode outside the MOV/ALU blocks is listed above. */
                break;
        }
    }

    cpu->cycles += (uint64_t)cpu_opcode_cycles(op, taken);
}
