#include "kernel.h"

#include "../bios/bios.h"
#include "../emu/mem.h"

#include <stddef.h>
#include <stdio.h>

#define KERNEL_META_STATE_ADDR 0xFF00u
#define KERNEL_META_STEPS_ADDR 0xFF01u
#define KERNEL_META_DIRTY_ADDR 0xFF10u
#define KERNEL_DIRTY_BYTES 32u

static void kernel_drain_bios_output(void) {
    while (bios_pending_output()) {
        (void)putchar((unsigned char)bios_get_output());
    }
    (void)fflush(stdout);
}

static void kernel_write_meta(const kernel_t *k) {
    uint32_t steps32 = (uint32_t)(k->steps & 0xFFFFFFFFu);
    unsigned int i;

    mem_write(KERNEL_META_STATE_ADDR, (uint8_t)k->state);
    mem_write(KERNEL_META_STEPS_ADDR + 0u, (uint8_t)(steps32 & 0xFFu));
    mem_write(KERNEL_META_STEPS_ADDR + 1u, (uint8_t)((steps32 >> 8) & 0xFFu));
    mem_write(KERNEL_META_STEPS_ADDR + 2u, (uint8_t)((steps32 >> 16) & 0xFFu));
    mem_write(KERNEL_META_STEPS_ADDR + 3u, (uint8_t)((steps32 >> 24) & 0xFFu));

    for (i = 0; i < KERNEL_DIRTY_BYTES; ++i) {
        mem_write((addr_t)(KERNEL_META_DIRTY_ADDR + i), 0u);
    }
}

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
    kernel_state_t resume_state = KS_SHELL;

    if (k == NULL) {
        return;
    }

    while (k->state != KS_HALT) {
        k->tick++;
        kernel_write_meta(k);

        switch (k->state) {
            case KS_BOOT:
                /* Placeholder boot image until shell loader exists. */
                mem_write(0x0100u, 0x76u); /* HLT */
                k->cpu.pc = 0x0100u;
                k->state = KS_SHELL;
                break;

            case KS_IDLE:
                kernel_drain_bios_output();
                k->state = KS_SHELL;
                break;

            case KS_SHELL:
                cpu_step(&k->cpu);
                k->steps++;
                if (k->cpu.io_out_pending != 0u && k->cpu.io_out_port == 0x01u) {
                    resume_state = KS_SHELL;
                    k->state = KS_SYSCALL;
                    break;
                }
                if (cpu_halted(&k->cpu)) {
                    k->state = KS_HALT;
                }
                kernel_drain_bios_output();
                break;

            case KS_RUNNING:
                cpu_step(&k->cpu);
                k->steps++;
                if (k->cpu.io_out_pending != 0u && k->cpu.io_out_port == 0x01u) {
                    resume_state = KS_RUNNING;
                    k->state = KS_SYSCALL;
                    break;
                }
                if (cpu_halted(&k->cpu)) {
                    k->state = KS_SHELL;
                }
                kernel_drain_bios_output();
                break;

            case KS_SYSCALL:
                bios_dispatch(&k->cpu);
                k->state = resume_state;
                kernel_drain_bios_output();
                break;

            case KS_HALT:
            default:
                break;
        }
    }

    kernel_write_meta(k);
}

kernel_state_t kernel_state(const kernel_t *k) {
    if (k == NULL) {
        return KS_HALT;
    }
    return k->state;
}
