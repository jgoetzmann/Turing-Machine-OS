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
    mem_init();
    cpu_init(&cpu);

    /* IN 0x42 should load from cpu.io_in_ports[0x42] */
    cpu.io_in_ports[0x42u] = 0xA5u;
    mem_write(0x0000u, 0xDBu);
    mem_write(0x0001u, 0x42u);
    cpu_step(&cpu);
    assert_u8("IN A", 0xA5u, cpu.a);
    assert_u16("IN PC", 0x0002u, cpu.pc);

    /* OUT 0x01 should expose pending/port/value for kernel */
    cpu.a = 0x7Eu;
    mem_write(0x0002u, 0xD3u);
    mem_write(0x0003u, 0x01u);
    cpu_step(&cpu);
    assert_u8("OUT pending", 1u, cpu.io_out_pending);
    assert_u8("OUT port", 0x01u, cpu.io_out_port);
    assert_u8("OUT value", 0x7Eu, cpu.io_out_value);
    assert_u16("OUT PC", 0x0004u, cpu.pc);

    /* Kernel should be able to clear pending and observe next OUT */
    cpu.io_out_pending = 0u;
    cpu.a = 0x11u;
    mem_write(0x0004u, 0xD3u);
    mem_write(0x0005u, 0x99u);
    cpu_step(&cpu);
    assert_u8("OUT2 pending", 1u, cpu.io_out_pending);
    assert_u8("OUT2 port", 0x99u, cpu.io_out_port);
    assert_u8("OUT2 value", 0x11u, cpu.io_out_value);

    puts("PASS: test_cpu_io");
    return 0;
}
