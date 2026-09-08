#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

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
    mem_init(1u, 65536u);
    cpu_init(&cpu);

    /* EI / DI */
    mem_write(0x0000u, 0xFBu); /* EI */
    mem_write(0x0001u, 0xF3u); /* DI */
    cpu_step(&cpu);
    assert_u8("EI sets interrupt flag", 1u, cpu.interrupts_enabled);
    cpu_step(&cpu);
    assert_u8("DI clears interrupt flag", 0u, cpu.interrupts_enabled);

    /* 20H and 30H are RIM and SIM on the 8085. On an 8080 they are NOPs (SPEC WS1-05), so they
       must leave A, rim_value and sim_value exactly as they were. */
    cpu.a = 0xA5u;
    cpu.rim_value = 0x5Au;
    cpu.sim_value = 0x11u;
    mem_write(0x0002u, 0x20u); /* 8085 RIM */
    cpu_step(&cpu);
    assert_u8("20H leaves A alone", 0xA5u, cpu.a);
    mem_write(0x0003u, 0x30u); /* 8085 SIM */
    cpu_step(&cpu);
    assert_u8("30H leaves sim_value alone", 0x11u, cpu.sim_value);
    assert_u8("30H leaves A alone", 0xA5u, cpu.a);

    /* RST 3 pushes return and jumps to 0x18 */
    cpu.sp = 0x2100u;
    cpu.pc = 0x0010u;
    mem_write(0x0010u, 0xDFu); /* RST 3 */
    cpu_step(&cpu);
    assert_u16("RST target", 0x0018u, cpu.pc);
    assert_u16("RST sp", 0x20FEu, cpu.sp);
    assert_u16("RST pushed return", 0x0011u, (uint16_t)(mem_read(0x20FEu) | ((uint16_t)mem_read(0x20FFu) << 8u)));

    puts("PASS: test_cpu_control");
    return 0;
}
