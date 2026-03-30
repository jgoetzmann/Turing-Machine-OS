#ifndef TURINGOS_KERNEL_H
#define TURINGOS_KERNEL_H

#include "../emu/cpu.h"

#include <stdint.h>

#define TAPE_SNAP_INTERVAL 1000u

typedef enum {
    KS_BOOT = 0,
    KS_IDLE = 1,
    KS_SHELL = 2,
    KS_RUNNING = 3,
    KS_SYSCALL = 4,
    KS_HALT = 5
} kernel_state_t;

typedef struct {
    kernel_state_t state;
    cpu_t cpu;
    uint64_t steps;
    uint32_t tick;
} kernel_t;

void kernel_init(kernel_t *k);
void kernel_run(kernel_t *k);
kernel_state_t kernel_state(const kernel_t *k);

#endif
