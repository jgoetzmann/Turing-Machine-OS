#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void assert_u8(uint8_t expected, uint8_t actual, const char *label) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%02X actual=0x%02X\n", label, expected, actual);
        exit(1);
    }
}

static void assert_u16(uint16_t expected, uint16_t actual, const char *label) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%04X actual=0x%04X\n", label, expected, actual);
        exit(1);
    }
}

int main(void) {
    cpu_t cpu;
    mem_init(1u, 65536u);
    cpu_init(&cpu);

    /* Data transfer group: MVI A,d8 */
    mem_write(0x0000u, 0x3Eu);
    mem_write(0x0001u, 0x5Au);
    cpu_step(&cpu);
    assert_u8(0x5Au, cpu.a, "MVI A");

    /* Arithmetic group: ADI d8 */
    mem_write(0x0002u, 0xC6u);
    mem_write(0x0003u, 0x01u);
    cpu_step(&cpu);
    assert_u8(0x5Bu, cpu.a, "ADI");

    /* Logic group: ORI d8 */
    mem_write(0x0004u, 0xF6u);
    mem_write(0x0005u, 0x80u);
    cpu_step(&cpu);
    assert_u8(0xDBu, cpu.a, "ORI");

    /* Branch group: JMP */
    mem_write(0x0006u, 0xC3u);
    mem_write(0x0007u, 0x20u);
    mem_write(0x0008u, 0x00u);
    cpu_step(&cpu);
    assert_u16(0x0020u, cpu.pc, "JMP");

    /* Stack group: PUSH/POP B */
    cpu.b = 0x12u;
    cpu.c = 0x34u;
    cpu.sp = 0x2100u;
    mem_write(0x0020u, 0xC5u);
    mem_write(0x0021u, 0xC1u);
    cpu_step(&cpu);
    cpu.b = 0;
    cpu.c = 0;
    cpu_step(&cpu);
    assert_u8(0x12u, cpu.b, "POP B restored high");
    assert_u8(0x34u, cpu.c, "POP B restored low");

    /* I/O group: OUT */
    cpu.a = 0xA5u;
    mem_write(0x0022u, 0xD3u);
    mem_write(0x0023u, 0x01u);
    cpu_step(&cpu);
    if (cpu.io_out_pending == 0u) {
        fail("OUT did not set pending");
    }
    assert_u8(0x01u, cpu.io_out_port, "OUT port");
    assert_u8(0xA5u, cpu.io_out_value, "OUT value");

    /* Control group: RST 1 */
    cpu.sp = 0x2200u;
    mem_write(0x0024u, 0xCFu);
    cpu_step(&cpu);
    assert_u16(0x0008u, cpu.pc, "RST vector");

    puts("PASS: test_opcodes");
    return 0;
}
