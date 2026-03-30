#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

#define FLAG_S  0x80u
#define FLAG_Z  0x40u
#define FLAG_AC 0x10u
#define FLAG_P  0x04u
#define FLAG_CY 0x01u

static void assert_u8(const char *label, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%02X actual=0x%02X\n", label, expected, actual);
        exit(1);
    }
}

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
    cpu.sp = 0x3000u;

    /* PUSH/POP B */
    cpu.b = 0x12u;
    cpu.c = 0x34u;
    mem_write(0x0000u, 0xC5u); /* PUSH B */
    mem_write(0x0001u, 0xC1u); /* POP B */
    cpu_step(&cpu);
    assert_u16("PUSH B SP", 0x2FFEu, cpu.sp);
    assert_u8("PUSH B hi", 0x12u, mem_read(0x2FFFu));
    assert_u8("PUSH B lo", 0x34u, mem_read(0x2FFEu));
    cpu.b = 0;
    cpu.c = 0;
    cpu_step(&cpu);
    assert_u16("POP B SP", 0x3000u, cpu.sp);
    assert_u8("POP B B", 0x12u, cpu.b);
    assert_u8("POP B C", 0x34u, cpu.c);

    /* PUSH/POP PSW, validate bit packing */
    cpu.a = 0xABu;
    cpu.flags = (uint8_t)(FLAG_S | FLAG_Z | FLAG_AC | FLAG_P | FLAG_CY);
    mem_write(0x0002u, 0xF5u); /* PUSH PSW */
    mem_write(0x0003u, 0xF1u); /* POP PSW */
    cpu_step(&cpu);
    assert_u16("PUSH PSW SP", 0x2FFEu, cpu.sp);
    assert_u8("PUSH PSW A saved", 0xABu, mem_read(0x2FFFu));
    assert_u8("PUSH PSW flags packed", 0xD7u, mem_read(0x2FFEu)); /* S Z 0 AC 0 P 1 CY */

    cpu.a = 0;
    cpu.flags = 0;
    cpu_step(&cpu);
    assert_u16("POP PSW SP", 0x3000u, cpu.sp);
    assert_u8("POP PSW A", 0xABu, cpu.a);
    assert_u8("POP PSW flags", (uint8_t)(FLAG_S | FLAG_Z | FLAG_AC | FLAG_P | FLAG_CY), cpu.flags);

    puts("PASS: test_cpu_stack");
    return 0;
}
