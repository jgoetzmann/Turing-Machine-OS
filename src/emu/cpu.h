#ifndef TURINGOS_CPU_H
#define TURINGOS_CPU_H
/* Intel 8080 core. Struct layout is frozen (exported to JS via tools/dump_layout). */
#include <stdint.h>

typedef struct {
    uint8_t a, b, c, d, e, h, l;
    uint16_t sp, pc;
    uint8_t flags;
    int halted;
    uint8_t io_out_pending;
    uint8_t io_out_port;
    uint8_t io_out_value;
    uint8_t io_in_ports[256];
    uint8_t interrupts_enabled;
    uint8_t rim_value;
    uint8_t sim_value;
    uint64_t cycles;          /* real 8080 cycle count */
} cpu_t;

void    cpu_init(cpu_t *cpu);
void    cpu_step(cpu_t *cpu);             /* exactly one instruction; every one of the 256 opcodes is handled */
void    cpu_reset(cpu_t *cpu);
int     cpu_halted(const cpu_t *cpu);
uint8_t cpu_opcode_len(uint8_t opcode);   /* 1..3 */
uint8_t cpu_opcode_cycles(uint8_t opcode, int taken);  /* Intel table; conditional CALL/RET differ by `taken` */

#endif
