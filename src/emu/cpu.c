#include "cpu.h"

#include "mem.h"

#include <stddef.h>
#include <string.h>

#define FLAG_S  0x80u
#define FLAG_Z  0x40u
#define FLAG_AC 0x10u
#define FLAG_P  0x04u
#define FLAG_CY 0x01u

static uint16_t get_hl(const cpu_t *cpu) {
    return (uint16_t)(((uint16_t)cpu->h << 8u) | cpu->l);
}

static uint8_t read_reg_by_code(const cpu_t *cpu, uint8_t reg_code);
static void write_reg_by_code(cpu_t *cpu, uint8_t reg_code, uint8_t value);

static uint16_t read_u16(uint16_t addr) {
    return (uint16_t)(mem_read(addr) | ((uint16_t)mem_read((uint16_t)(addr + 1u)) << 8u));
}

static void push_u16(cpu_t *cpu, uint16_t value) {
    const uint8_t lo = (uint8_t)(value & 0xFFu);
    const uint8_t hi = (uint8_t)(value >> 8u);
    mem_write((uint16_t)(cpu->sp - 1u), hi);
    mem_write((uint16_t)(cpu->sp - 2u), lo);
    cpu->sp = (uint16_t)(cpu->sp - 2u);
}

static uint16_t pop_u16(cpu_t *cpu) {
    const uint8_t lo = mem_read(cpu->sp);
    const uint8_t hi = mem_read((uint16_t)(cpu->sp + 1u));
    cpu->sp = (uint16_t)(cpu->sp + 2u);
    return (uint16_t)(lo | ((uint16_t)hi << 8u));
}

static uint8_t branch_condition_true(const cpu_t *cpu, uint8_t cond_code) {
    switch (cond_code & 0x07u) {
        case 0x00u: return (uint8_t)((cpu->flags & FLAG_Z) == 0u); /* NZ */
        case 0x01u: return (uint8_t)((cpu->flags & FLAG_Z) != 0u); /* Z */
        case 0x02u: return (uint8_t)((cpu->flags & FLAG_CY) == 0u); /* NC */
        case 0x03u: return (uint8_t)((cpu->flags & FLAG_CY) != 0u); /* C */
        case 0x04u: return (uint8_t)((cpu->flags & FLAG_P) == 0u); /* PO */
        case 0x05u: return (uint8_t)((cpu->flags & FLAG_P) != 0u); /* PE */
        case 0x06u: return (uint8_t)((cpu->flags & FLAG_S) == 0u); /* P */
        case 0x07u: return (uint8_t)((cpu->flags & FLAG_S) != 0u); /* M */
        default: return 0u;
    }
}

static uint8_t pack_psw_flags(uint8_t flags) {
    uint8_t psw = 0x02u;
    if ((flags & FLAG_S) != 0u) psw |= FLAG_S;
    if ((flags & FLAG_Z) != 0u) psw |= FLAG_Z;
    if ((flags & FLAG_AC) != 0u) psw |= FLAG_AC;
    if ((flags & FLAG_P) != 0u) psw |= FLAG_P;
    if ((flags & FLAG_CY) != 0u) psw |= FLAG_CY;
    return psw;
}

static uint8_t unpack_psw_flags(uint8_t psw) {
    uint8_t flags = 0u;
    if ((psw & FLAG_S) != 0u) flags |= FLAG_S;
    if ((psw & FLAG_Z) != 0u) flags |= FLAG_Z;
    if ((psw & FLAG_AC) != 0u) flags |= FLAG_AC;
    if ((psw & FLAG_P) != 0u) flags |= FLAG_P;
    if ((psw & FLAG_CY) != 0u) flags |= FLAG_CY;
    return flags;
}

static uint8_t parity_even(uint8_t value) {
    uint8_t p = 0u;
    while (value != 0u) {
        p ^= (uint8_t)(value & 1u);
        value >>= 1u;
    }
    return (uint8_t)(p == 0u);
}

static void set_szp(cpu_t *cpu, uint8_t result) {
    cpu->flags &= (uint8_t)~(FLAG_S | FLAG_Z | FLAG_P);
    if ((result & 0x80u) != 0u) {
        cpu->flags |= FLAG_S;
    }
    if (result == 0u) {
        cpu->flags |= FLAG_Z;
    }
    if (parity_even(result)) {
        cpu->flags |= FLAG_P;
    }
}

static uint16_t get_rp(const cpu_t *cpu, uint8_t rp) {
    switch (rp & 0x03u) {
        case 0x00u: return (uint16_t)(((uint16_t)cpu->b << 8u) | cpu->c);
        case 0x01u: return (uint16_t)(((uint16_t)cpu->d << 8u) | cpu->e);
        case 0x02u: return get_hl(cpu);
        case 0x03u: return cpu->sp;
        default: return 0u;
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
        case 0x03u:
            cpu->sp = value;
            break;
        default:
            break;
    }
}

static void add_to_a(cpu_t *cpu, uint8_t value, uint8_t carry_in) {
    const uint16_t sum = (uint16_t)cpu->a + (uint16_t)value + (uint16_t)carry_in;
    const uint8_t result = (uint8_t)(sum & 0xFFu);
    const uint8_t ac = (uint8_t)(((cpu->a & 0x0Fu) + (value & 0x0Fu) + carry_in) > 0x0Fu);
    cpu->a = result;
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~(FLAG_CY | FLAG_AC);
    if (sum > 0xFFu) {
        cpu->flags |= FLAG_CY;
    }
    if (ac) {
        cpu->flags |= FLAG_AC;
    }
}

static void sub_from_a(cpu_t *cpu, uint8_t value, uint8_t borrow_in) {
    const uint16_t sub = (uint16_t)value + (uint16_t)borrow_in;
    const uint16_t diff = (uint16_t)cpu->a - sub;
    const uint8_t result = (uint8_t)(diff & 0xFFu);
    const uint8_t cy = (uint8_t)((uint16_t)cpu->a < sub);
    const uint8_t ac = (uint8_t)((cpu->a & 0x0Fu) < ((value & 0x0Fu) + borrow_in));
    cpu->a = result;
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~(FLAG_CY | FLAG_AC);
    if (cy) {
        cpu->flags |= FLAG_CY;
    }
    if (ac) {
        cpu->flags |= FLAG_AC;
    }
}

static void inr_reg(cpu_t *cpu, uint8_t reg_code) {
    const uint8_t before = read_reg_by_code(cpu, reg_code);
    const uint8_t result = (uint8_t)(before + 1u);
    write_reg_by_code(cpu, reg_code, result);
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~FLAG_AC;
    if (((before & 0x0Fu) + 1u) > 0x0Fu) {
        cpu->flags |= FLAG_AC;
    }
}

static void dcr_reg(cpu_t *cpu, uint8_t reg_code) {
    const uint8_t before = read_reg_by_code(cpu, reg_code);
    const uint8_t result = (uint8_t)(before - 1u);
    write_reg_by_code(cpu, reg_code, result);
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~FLAG_AC;
    if ((before & 0x0Fu) == 0x00u) {
        cpu->flags |= FLAG_AC;
    }
}

static void logic_flags(cpu_t *cpu, uint8_t result) {
    cpu->a = result;
    cpu->flags &= (uint8_t)~(FLAG_CY | FLAG_AC);
    set_szp(cpu, result);
}

static void logic_flags_and(cpu_t *cpu, uint8_t result) {
    cpu->a = result;
    cpu->flags &= (uint8_t)~FLAG_CY;
    cpu->flags |= FLAG_AC; /* 8080 ANA/ANI set AC */
    set_szp(cpu, result);
}

static void cmp_flags_only(cpu_t *cpu, uint8_t value) {
    const uint8_t a = cpu->a;
    const uint16_t sub = (uint16_t)value;
    const uint16_t diff = (uint16_t)a - sub;
    const uint8_t result = (uint8_t)(diff & 0xFFu);
    const uint8_t cy = (uint8_t)(a < value);
    const uint8_t ac = (uint8_t)((a & 0x0Fu) < (value & 0x0Fu));
    set_szp(cpu, result);
    cpu->flags &= (uint8_t)~(FLAG_CY | FLAG_AC);
    if (cy) {
        cpu->flags |= FLAG_CY;
    }
    if (ac) {
        cpu->flags |= FLAG_AC;
    }
}

static void daa(cpu_t *cpu) {
    uint8_t adjust = 0u;
    uint8_t new_cy = (uint8_t)((cpu->flags & FLAG_CY) != 0u);
    if (((cpu->a & 0x0Fu) > 9u) || ((cpu->flags & FLAG_AC) != 0u)) {
        adjust |= 0x06u;
    }
    if ((cpu->a > 0x99u) || ((cpu->flags & FLAG_CY) != 0u)) {
        adjust |= 0x60u;
        new_cy = 1u;
    }
    {
        const uint16_t sum = (uint16_t)cpu->a + (uint16_t)adjust;
        const uint8_t result = (uint8_t)(sum & 0xFFu);
        const uint8_t ac = (uint8_t)(((cpu->a & 0x0Fu) + (adjust & 0x0Fu)) > 0x0Fu);
        cpu->a = result;
        set_szp(cpu, result);
        cpu->flags &= (uint8_t)~(FLAG_CY | FLAG_AC);
        if (new_cy) {
            cpu->flags |= FLAG_CY;
        }
        if (ac) {
            cpu->flags |= FLAG_AC;
        }
    }
}

static uint8_t read_reg_by_code(const cpu_t *cpu, uint8_t reg_code) {
    switch (reg_code & 0x07u) {
        case 0x00u: return cpu->b;
        case 0x01u: return cpu->c;
        case 0x02u: return cpu->d;
        case 0x03u: return cpu->e;
        case 0x04u: return cpu->h;
        case 0x05u: return cpu->l;
        case 0x06u: return mem_read(get_hl(cpu));
        case 0x07u: return cpu->a;
        default: return 0u;
    }
}

static void write_reg_by_code(cpu_t *cpu, uint8_t reg_code, uint8_t value) {
    switch (reg_code & 0x07u) {
        case 0x00u: cpu->b = value; break;
        case 0x01u: cpu->c = value; break;
        case 0x02u: cpu->d = value; break;
        case 0x03u: cpu->e = value; break;
        case 0x04u: cpu->h = value; break;
        case 0x05u: cpu->l = value; break;
        case 0x06u: mem_write(get_hl(cpu), value); break;
        case 0x07u: cpu->a = value; break;
        default: break;
    }
}

void cpu_init(cpu_t *cpu) {
    if (cpu == NULL) {
        return;
    }
    memset(cpu, 0, sizeof(*cpu));
    cpu->sp = 0xFDFFu;
}

void cpu_reset(cpu_t *cpu) {
    cpu_init(cpu);
}

void cpu_step(cpu_t *cpu) {
    if (cpu == NULL || cpu->halted) {
        return;
    }

    {
        const uint8_t opcode = mem_read(cpu->pc);
        const uint16_t next_pc = (uint16_t)(cpu->pc + 1u);

        switch (opcode) {
            case 0x00u: /* NOP */
                cpu->pc = next_pc;
                break;

            case 0x76u: /* HLT */
                cpu->halted = 1;
                cpu->pc = next_pc;
                break;

            case 0x01u: /* LXI B,d16 */
                cpu->c = mem_read(next_pc);
                cpu->b = mem_read((uint16_t)(next_pc + 1u));
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            case 0x11u: /* LXI D,d16 */
                cpu->e = mem_read(next_pc);
                cpu->d = mem_read((uint16_t)(next_pc + 1u));
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            case 0x21u: /* LXI H,d16 */
                cpu->l = mem_read(next_pc);
                cpu->h = mem_read((uint16_t)(next_pc + 1u));
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            case 0x31u: /* LXI SP,d16 */
                cpu->sp = (uint16_t)(mem_read(next_pc) |
                                     ((uint16_t)mem_read((uint16_t)(next_pc + 1u)) << 8u));
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;

            case 0x06u: /* MVI B,d8 */
            case 0x0Eu: /* MVI C,d8 */
            case 0x16u: /* MVI D,d8 */
            case 0x1Eu: /* MVI E,d8 */
            case 0x26u: /* MVI H,d8 */
            case 0x2Eu: /* MVI L,d8 */
            case 0x36u: /* MVI M,d8 */
            case 0x3Eu: /* MVI A,d8 */
                write_reg_by_code(cpu, (uint8_t)((opcode >> 3u) & 0x07u), mem_read(next_pc));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;

            case 0x3Au: { /* LDA addr */
                const uint16_t addr = (uint16_t)(mem_read(next_pc) |
                                                ((uint16_t)mem_read((uint16_t)(next_pc + 1u)) << 8u));
                cpu->a = mem_read(addr);
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            }
            case 0x32u: { /* STA addr */
                const uint16_t addr = (uint16_t)(mem_read(next_pc) |
                                                ((uint16_t)mem_read((uint16_t)(next_pc + 1u)) << 8u));
                mem_write(addr, cpu->a);
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            }
            case 0x2Au: { /* LHLD addr */
                const uint16_t addr = (uint16_t)(mem_read(next_pc) |
                                                ((uint16_t)mem_read((uint16_t)(next_pc + 1u)) << 8u));
                cpu->l = mem_read(addr);
                cpu->h = mem_read((uint16_t)(addr + 1u));
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            }
            case 0x22u: { /* SHLD addr */
                const uint16_t addr = (uint16_t)(mem_read(next_pc) |
                                                ((uint16_t)mem_read((uint16_t)(next_pc + 1u)) << 8u));
                mem_write(addr, cpu->l);
                mem_write((uint16_t)(addr + 1u), cpu->h);
                cpu->pc = (uint16_t)(next_pc + 2u);
                break;
            }
            case 0xEBu: { /* XCHG */
                const uint8_t old_d = cpu->d;
                const uint8_t old_e = cpu->e;
                cpu->d = cpu->h;
                cpu->e = cpu->l;
                cpu->h = old_d;
                cpu->l = old_e;
                cpu->pc = next_pc;
                break;
            }
            case 0xE3u: { /* XTHL */
                const uint8_t low = mem_read(cpu->sp);
                const uint8_t high = mem_read((uint16_t)(cpu->sp + 1u));
                mem_write(cpu->sp, cpu->l);
                mem_write((uint16_t)(cpu->sp + 1u), cpu->h);
                cpu->l = low;
                cpu->h = high;
                cpu->pc = next_pc;
                break;
            }
            case 0xF9u: /* SPHL */
                cpu->sp = get_hl(cpu);
                cpu->pc = next_pc;
                break;
            case 0xE9u: /* PCHL */
                cpu->pc = get_hl(cpu);
                break;
            case 0xC6u: /* ADI d8 */
                add_to_a(cpu, mem_read(next_pc), 0u);
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xCEu: /* ACI d8 */
                add_to_a(cpu, mem_read(next_pc), (uint8_t)((cpu->flags & FLAG_CY) != 0u));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xD6u: /* SUI d8 */
                sub_from_a(cpu, mem_read(next_pc), 0u);
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xDEu: /* SBI d8 */
                sub_from_a(cpu, mem_read(next_pc), (uint8_t)((cpu->flags & FLAG_CY) != 0u));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0x27u: /* DAA */
                daa(cpu);
                cpu->pc = next_pc;
                break;

            case 0x07u: { /* RLC */
                const uint8_t a = cpu->a;
                const uint8_t cy = (uint8_t)((a >> 7u) & 1u);
                cpu->a = (uint8_t)((a << 1u) | cy);
                cpu->flags &= (uint8_t)~FLAG_CY;
                if (cy) {
                    cpu->flags |= FLAG_CY;
                }
                cpu->pc = next_pc;
                break;
            }
            case 0x0Fu: { /* RRC */
                const uint8_t a = cpu->a;
                const uint8_t cy = (uint8_t)(a & 1u);
                cpu->a = (uint8_t)((a >> 1u) | (uint8_t)(cy << 7u));
                cpu->flags &= (uint8_t)~FLAG_CY;
                if (cy) {
                    cpu->flags |= FLAG_CY;
                }
                cpu->pc = next_pc;
                break;
            }
            case 0x17u: { /* RAL */
                const uint8_t a = cpu->a;
                const uint8_t old_cy = (uint8_t)((cpu->flags & FLAG_CY) != 0u);
                const uint8_t new_cy = (uint8_t)((a >> 7u) & 1u);
                cpu->a = (uint8_t)((a << 1u) | old_cy);
                cpu->flags &= (uint8_t)~FLAG_CY;
                if (new_cy) {
                    cpu->flags |= FLAG_CY;
                }
                cpu->pc = next_pc;
                break;
            }
            case 0x1Fu: { /* RAR */
                const uint8_t a = cpu->a;
                const uint8_t old_cy = (uint8_t)((cpu->flags & FLAG_CY) != 0u);
                const uint8_t new_cy = (uint8_t)(a & 1u);
                cpu->a = (uint8_t)((a >> 1u) | (uint8_t)(old_cy << 7u));
                cpu->flags &= (uint8_t)~FLAG_CY;
                if (new_cy) {
                    cpu->flags |= FLAG_CY;
                }
                cpu->pc = next_pc;
                break;
            }
            case 0x2Fu: /* CMA */
                cpu->a = (uint8_t)~cpu->a;
                cpu->pc = next_pc;
                break;
            case 0x37u: /* STC */
                cpu->flags |= FLAG_CY;
                cpu->pc = next_pc;
                break;
            case 0x3Fu: /* CMC */
                cpu->flags ^= FLAG_CY;
                cpu->pc = next_pc;
                break;

            case 0xE6u: /* ANI d8 */
                logic_flags_and(cpu, (uint8_t)(cpu->a & mem_read(next_pc)));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xEEu: /* XRI d8 */
                logic_flags(cpu, (uint8_t)(cpu->a ^ mem_read(next_pc)));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xF6u: /* ORI d8 */
                logic_flags(cpu, (uint8_t)(cpu->a | mem_read(next_pc)));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xFEu: /* CPI d8 */
                cmp_flags_only(cpu, mem_read(next_pc));
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            case 0xC3u: /* JMP addr */
                cpu->pc = read_u16(next_pc);
                break;
            case 0xCDu: { /* CALL addr */
                const uint16_t target = read_u16(next_pc);
                push_u16(cpu, (uint16_t)(next_pc + 2u));
                cpu->pc = target;
                break;
            }
            case 0xC9u: /* RET */
                cpu->pc = pop_u16(cpu);
                break;
            case 0xDBu: { /* IN port */
                const uint8_t port = mem_read(next_pc);
                cpu->a = cpu->io_in_ports[port];
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            }
            case 0xD3u: { /* OUT port */
                const uint8_t port = mem_read(next_pc);
                cpu->io_out_pending = 1u;
                cpu->io_out_port = port;
                cpu->io_out_value = cpu->a;
                cpu->pc = (uint16_t)(next_pc + 1u);
                break;
            }
            case 0xFBu: /* EI */
                cpu->interrupts_enabled = 1u;
                cpu->pc = next_pc;
                break;
            case 0xF3u: /* DI */
                cpu->interrupts_enabled = 0u;
                cpu->pc = next_pc;
                break;
            case 0x20u: /* RIM */
                cpu->a = cpu->rim_value;
                cpu->pc = next_pc;
                break;
            case 0x30u: /* SIM */
                cpu->sim_value = cpu->a;
                cpu->pc = next_pc;
                break;

            default:
                if ((opcode >= 0x80u) && (opcode <= 0x87u)) { /* ADD r */
                    add_to_a(cpu, read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u)), 0u);
                    cpu->pc = next_pc;
                } else if ((opcode >= 0x88u) && (opcode <= 0x8Fu)) { /* ADC r */
                    add_to_a(cpu, read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u)),
                             (uint8_t)((cpu->flags & FLAG_CY) != 0u));
                    cpu->pc = next_pc;
                } else if ((opcode >= 0x90u) && (opcode <= 0x97u)) { /* SUB r */
                    sub_from_a(cpu, read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u)), 0u);
                    cpu->pc = next_pc;
                } else if ((opcode >= 0x98u) && (opcode <= 0x9Fu)) { /* SBB r */
                    sub_from_a(cpu, read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u)),
                               (uint8_t)((cpu->flags & FLAG_CY) != 0u));
                    cpu->pc = next_pc;
                } else if ((opcode == 0x04u) || (opcode == 0x0Cu) || (opcode == 0x14u) ||
                           (opcode == 0x1Cu) || (opcode == 0x24u) || (opcode == 0x2Cu) ||
                           (opcode == 0x34u) || (opcode == 0x3Cu)) { /* INR r */
                    inr_reg(cpu, (uint8_t)((opcode >> 3u) & 0x07u));
                    cpu->pc = next_pc;
                } else if ((opcode == 0x05u) || (opcode == 0x0Du) || (opcode == 0x15u) ||
                           (opcode == 0x1Du) || (opcode == 0x25u) || (opcode == 0x2Du) ||
                           (opcode == 0x35u) || (opcode == 0x3Du)) { /* DCR r */
                    dcr_reg(cpu, (uint8_t)((opcode >> 3u) & 0x07u));
                    cpu->pc = next_pc;
                } else if ((opcode == 0x03u) || (opcode == 0x13u) || (opcode == 0x23u) ||
                           (opcode == 0x33u)) { /* INX rp */
                    const uint8_t rp = (uint8_t)((opcode >> 4u) & 0x03u);
                    set_rp(cpu, rp, (uint16_t)(get_rp(cpu, rp) + 1u));
                    cpu->pc = next_pc;
                } else if ((opcode == 0x0Bu) || (opcode == 0x1Bu) || (opcode == 0x2Bu) ||
                           (opcode == 0x3Bu)) { /* DCX rp */
                    const uint8_t rp = (uint8_t)((opcode >> 4u) & 0x03u);
                    set_rp(cpu, rp, (uint16_t)(get_rp(cpu, rp) - 1u));
                    cpu->pc = next_pc;
                } else if ((opcode == 0x09u) || (opcode == 0x19u) || (opcode == 0x29u) ||
                           (opcode == 0x39u)) { /* DAD rp */
                    const uint8_t rp = (uint8_t)((opcode >> 4u) & 0x03u);
                    const uint32_t sum = (uint32_t)get_hl(cpu) + (uint32_t)get_rp(cpu, rp);
                    cpu->h = (uint8_t)((sum >> 8u) & 0xFFu);
                    cpu->l = (uint8_t)(sum & 0xFFu);
                    cpu->flags &= (uint8_t)~FLAG_CY;
                    if ((sum & 0x10000u) != 0u) {
                        cpu->flags |= FLAG_CY;
                    }
                    cpu->pc = next_pc;
                } else if ((opcode >= 0xA0u) && (opcode <= 0xA7u)) { /* ANA r */
                    logic_flags_and(cpu, (uint8_t)(cpu->a & read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u))));
                    cpu->pc = next_pc;
                } else if ((opcode >= 0xA8u) && (opcode <= 0xAFu)) { /* XRA r */
                    logic_flags(cpu, (uint8_t)(cpu->a ^ read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u))));
                    cpu->pc = next_pc;
                } else if ((opcode >= 0xB0u) && (opcode <= 0xB7u)) { /* ORA r */
                    logic_flags(cpu, (uint8_t)(cpu->a | read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u))));
                    cpu->pc = next_pc;
                } else if ((opcode >= 0xB8u) && (opcode <= 0xBFu)) { /* CMP r */
                    cmp_flags_only(cpu, read_reg_by_code(cpu, (uint8_t)(opcode & 0x07u)));
                    cpu->pc = next_pc;
                } else if ((opcode == 0xC2u) || (opcode == 0xCAu) || (opcode == 0xD2u) ||
                           (opcode == 0xDAu) || (opcode == 0xE2u) || (opcode == 0xEAu) ||
                           (opcode == 0xF2u) || (opcode == 0xFAu)) { /* Jccc addr */
                    const uint8_t cond = (uint8_t)((opcode >> 3u) & 0x07u);
                    if (branch_condition_true(cpu, cond)) {
                        cpu->pc = read_u16(next_pc);
                    } else {
                        cpu->pc = (uint16_t)(next_pc + 2u);
                    }
                } else if ((opcode == 0xC4u) || (opcode == 0xCCu) || (opcode == 0xD4u) ||
                           (opcode == 0xDCu) || (opcode == 0xE4u) || (opcode == 0xECu) ||
                           (opcode == 0xF4u) || (opcode == 0xFCu)) { /* Cccc addr */
                    const uint8_t cond = (uint8_t)((opcode >> 3u) & 0x07u);
                    if (branch_condition_true(cpu, cond)) {
                        push_u16(cpu, (uint16_t)(next_pc + 2u));
                        cpu->pc = read_u16(next_pc);
                    } else {
                        cpu->pc = (uint16_t)(next_pc + 2u);
                    }
                } else if ((opcode == 0xC0u) || (opcode == 0xC8u) || (opcode == 0xD0u) ||
                           (opcode == 0xD8u) || (opcode == 0xE0u) || (opcode == 0xE8u) ||
                           (opcode == 0xF0u) || (opcode == 0xF8u)) { /* Rccc */
                    const uint8_t cond = (uint8_t)((opcode >> 3u) & 0x07u);
                    if (branch_condition_true(cpu, cond)) {
                        cpu->pc = pop_u16(cpu);
                    } else {
                        cpu->pc = next_pc;
                    }
                } else if ((opcode == 0xC5u) || (opcode == 0xD5u) ||
                           (opcode == 0xE5u) || (opcode == 0xF5u)) { /* PUSH rp/psw */
                    switch (opcode) {
                        case 0xC5u:
                            push_u16(cpu, (uint16_t)(((uint16_t)cpu->b << 8u) | cpu->c));
                            break;
                        case 0xD5u:
                            push_u16(cpu, (uint16_t)(((uint16_t)cpu->d << 8u) | cpu->e));
                            break;
                        case 0xE5u:
                            push_u16(cpu, (uint16_t)(((uint16_t)cpu->h << 8u) | cpu->l));
                            break;
                        case 0xF5u:
                            push_u16(cpu, (uint16_t)(((uint16_t)cpu->a << 8u) | pack_psw_flags(cpu->flags)));
                            break;
                        default:
                            break;
                    }
                    cpu->pc = next_pc;
                } else if ((opcode == 0xC1u) || (opcode == 0xD1u) ||
                           (opcode == 0xE1u) || (opcode == 0xF1u)) { /* POP rp/psw */
                    const uint16_t value = pop_u16(cpu);
                    switch (opcode) {
                        case 0xC1u:
                            cpu->b = (uint8_t)(value >> 8u);
                            cpu->c = (uint8_t)(value & 0xFFu);
                            break;
                        case 0xD1u:
                            cpu->d = (uint8_t)(value >> 8u);
                            cpu->e = (uint8_t)(value & 0xFFu);
                            break;
                        case 0xE1u:
                            cpu->h = (uint8_t)(value >> 8u);
                            cpu->l = (uint8_t)(value & 0xFFu);
                            break;
                        case 0xF1u:
                            cpu->a = (uint8_t)(value >> 8u);
                            cpu->flags = unpack_psw_flags((uint8_t)(value & 0xFFu));
                            break;
                        default:
                            break;
                    }
                    cpu->pc = next_pc;
                } else if ((opcode == 0xC7u) || (opcode == 0xCFu) || (opcode == 0xD7u) ||
                           (opcode == 0xDFu) || (opcode == 0xE7u) || (opcode == 0xEFu) ||
                           (opcode == 0xF7u) || (opcode == 0xFFu)) { /* RST n */
                    const uint8_t rst = (uint8_t)((opcode >> 3u) & 0x07u);
                    push_u16(cpu, next_pc);
                    cpu->pc = (uint16_t)(rst * 8u);
                } else
                if ((opcode >= 0x40u) && (opcode <= 0x7Fu)) { /* MOV dst,src (except 0x76) */
                    const uint8_t dst = (uint8_t)((opcode >> 3u) & 0x07u);
                    const uint8_t src = (uint8_t)(opcode & 0x07u);
                    write_reg_by_code(cpu, dst, read_reg_by_code(cpu, src));
                    cpu->pc = next_pc;
                } else {
                    /* Temporary fallback until full opcode table is implemented. */
                    cpu->pc = next_pc;
                }
                break;
        }
    }

    cpu->cycles += 1u;
}

int cpu_halted(const cpu_t *cpu) {
    if (cpu == NULL) {
        return 1;
    }
    return cpu->halted;
}
