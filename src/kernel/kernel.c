#include "kernel.h"

#include "../bios/bios.h"
#include "../emu/mem.h"

#include <stddef.h>

void kernel_init(kernel_t *k) {
    if (k == NULL) {
        return;
    }
    k->state = KS_BOOT;
    k->steps = 0;
    k->tick = 0;
    cpu_init(&k->cpu);
    mem_init();
    bios_init();
}

void kernel_run(kernel_t *k) {
    if (k == NULL) {
        return;
    }

    k->state = KS_HALT;
}

kernel_state_t kernel_state(const kernel_t *k) {
    if (k == NULL) {
        return KS_HALT;
    }
    return k->state;
}
