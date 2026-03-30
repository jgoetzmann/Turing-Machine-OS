#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

static void assert_u16(const char *label, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%04X actual=0x%04X\n", label, expected, actual);
        exit(1);
    }
}

int main(void) {
    cpu_t cpu;
    mem_init();
    cpu_init(&cpu);

    mem_write(0x0000u, 0x76u); /* HLT */
    cpu_step(&cpu);

    if (!cpu_halted(&cpu)) {
        fprintf(stderr, "FAIL: HLT did not set cpu->halted\n");
        return 1;
    }
    assert_u16("HLT advances PC by one", 0x0001u, cpu.pc);

    /* Additional step should be a no-op while halted. */
    cpu_step(&cpu);
    assert_u16("halted cpu_step should not advance PC", 0x0001u, cpu.pc);

    puts("PASS: test_cpu_hlt");
    return 0;
}
