#ifndef TURINGOS_CPU_H
#define TURINGOS_CPU_H

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
    uint64_t cycles;
} cpu_t;

void cpu_init(cpu_t *cpu);
void cpu_step(cpu_t *cpu);
void cpu_reset(cpu_t *cpu);
int cpu_halted(const cpu_t *cpu);

#endif
