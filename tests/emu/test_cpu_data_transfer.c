#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

static void fail_u8(const char *label, uint8_t expected, uint8_t actual) {
    fprintf(stderr, "FAIL: %s expected=0x%02X actual=0x%02X\n", label, expected, actual);
    exit(1);
}

static void fail_u16(const char *label, uint16_t expected, uint16_t actual) {
    fprintf(stderr, "FAIL: %s expected=0x%04X actual=0x%04X\n", label, expected, actual);
    exit(1);
}

static void assert_u8(const char *label, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fail_u8(label, expected, actual);
    }
}

static void assert_u16(const char *label, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fail_u16(label, expected, actual);
    }
}

int main(void) {
    cpu_t cpu;
    mem_init();
    cpu_init(&cpu);

    /* LXI H,0x3456 */
    mem_write(0x0000u, 0x21u);
    mem_write(0x0001u, 0x56u);
    mem_write(0x0002u, 0x34u);
    cpu_step(&cpu);
    assert_u8("LXI H high", 0x34u, cpu.h);
    assert_u8("LXI H low", 0x56u, cpu.l);
    assert_u16("LXI H pc", 0x0003u, cpu.pc);

    /* MVI M,0xAA */
    mem_write(0x0003u, 0x36u);
    mem_write(0x0004u, 0xAAu);
    cpu_step(&cpu);
    assert_u8("MVI M writes [HL]", 0xAAu, mem_read(0x3456u));
    assert_u16("MVI M pc", 0x0005u, cpu.pc);

    /* MOV A,M */
    mem_write(0x0005u, 0x7Eu);
    cpu_step(&cpu);
    assert_u8("MOV A,M", 0xAAu, cpu.a);
    assert_u16("MOV A,M pc", 0x0006u, cpu.pc);

    /* STA 0x4000 */
    mem_write(0x0006u, 0x32u);
    mem_write(0x0007u, 0x00u);
    mem_write(0x0008u, 0x40u);
    cpu_step(&cpu);
    assert_u8("STA writes absolute", 0xAAu, mem_read(0x4000u));
    assert_u16("STA pc", 0x0009u, cpu.pc);

    /* LDA 0x4000 */
    cpu.a = 0x00u;
    mem_write(0x0009u, 0x3Au);
    mem_write(0x000Au, 0x00u);
    mem_write(0x000Bu, 0x40u);
    cpu_step(&cpu);
    assert_u8("LDA loads A", 0xAAu, cpu.a);
    assert_u16("LDA pc", 0x000Cu, cpu.pc);

    /* XCHG (DE <-> HL) */
    cpu.d = 0x11u;
    cpu.e = 0x22u;
    cpu.h = 0x33u;
    cpu.l = 0x44u;
    mem_write(0x000Cu, 0xEBu);
    cpu_step(&cpu);
    assert_u8("XCHG d", 0x33u, cpu.d);
    assert_u8("XCHG e", 0x44u, cpu.e);
    assert_u8("XCHG h", 0x11u, cpu.h);
    assert_u8("XCHG l", 0x22u, cpu.l);

    /* SPHL */
    mem_write(0x000Du, 0xF9u);
    cpu_step(&cpu);
    assert_u16("SPHL", 0x1122u, cpu.sp);

    /* PCHL */
    cpu.h = 0x00u;
    cpu.l = 0x20u;
    mem_write(0x000Eu, 0x00u); /* ignored due to PCHL jump */
    mem_write(0x000Fu, 0x00u);
    mem_write(0x0010u, 0x00u);
    mem_write(0x0011u, 0x00u);
    mem_write(0x0012u, 0x00u);
    mem_write(0x0013u, 0x00u);
    mem_write(0x000Eu, 0xE9u);
    cpu.pc = 0x000Eu;
    cpu_step(&cpu);
    assert_u16("PCHL pc", 0x0020u, cpu.pc);

    /* HLT */
    mem_write(0x0020u, 0x76u);
    cpu_step(&cpu);
    if (!cpu_halted(&cpu)) {
        fprintf(stderr, "FAIL: HLT did not set halted\n");
        return 1;
    }

    puts("PASS: test_cpu_data_transfer");
    return 0;
}
