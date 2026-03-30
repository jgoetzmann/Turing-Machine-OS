#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

#define FLAG_Z  0x40u
#define FLAG_AC 0x10u
#define FLAG_CY 0x01u

static void assert_u8(const char *label, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%02X actual=0x%02X\n", label, expected, actual);
        exit(1);
    }
}

int main(void) {
    cpu_t cpu;
    mem_init();
    cpu_init(&cpu);

    /* MVI A,0xF0 ; ANI 0x0F => A=0x00, Z set, CY clear */
    mem_write(0x0000u, 0x3Eu);
    mem_write(0x0001u, 0xF0u);
    mem_write(0x0002u, 0xE6u);
    mem_write(0x0003u, 0x0Fu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("ANI", 0x00u, cpu.a);
    if ((cpu.flags & FLAG_Z) == 0u || (cpu.flags & FLAG_CY) != 0u || (cpu.flags & FLAG_AC) == 0u) {
        fprintf(stderr, "FAIL: ANI expected Z, AC set, CY clear\n");
        return 1;
    }

    /* MVI A,0xAA ; XRI 0xFF => A=0x55 */
    mem_write(0x0004u, 0x3Eu);
    mem_write(0x0005u, 0xAAu);
    mem_write(0x0006u, 0xEEu);
    mem_write(0x0007u, 0xFFu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("XRI", 0x55u, cpu.a);
    if ((cpu.flags & FLAG_AC) != 0u || (cpu.flags & FLAG_CY) != 0u) {
        fprintf(stderr, "FAIL: XRI expected AC and CY clear\n");
        return 1;
    }

    /* MVI A,0x0C ; ORI 0x30 => A=0x3C */
    mem_write(0x0008u, 0x3Eu);
    mem_write(0x0009u, 0x0Cu);
    mem_write(0x000Au, 0xF6u);
    mem_write(0x000Bu, 0x30u);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("ORI", 0x3Cu, cpu.a);
    if ((cpu.flags & FLAG_AC) != 0u || (cpu.flags & FLAG_CY) != 0u) {
        fprintf(stderr, "FAIL: ORI expected AC and CY clear\n");
        return 1;
    }

    /* CMP: A=0x3C, MVI B,0x3C ; CMP B => Z, A unchanged */
    mem_write(0x000Cu, 0x06u);
    mem_write(0x000Du, 0x3Cu);
    mem_write(0x000Eu, 0xB8u);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("CMP preserves A", 0x3Cu, cpu.a);
    if ((cpu.flags & FLAG_Z) == 0u) {
        fprintf(stderr, "FAIL: CMP equal expected Z\n");
        return 1;
    }

    /* CPI 0x01 with A=0x3C */
    mem_write(0x000Fu, 0xFEu);
    mem_write(0x0010u, 0x01u);
    cpu_step(&cpu);
    assert_u8("CPI preserves A", 0x3Cu, cpu.a);

    /* RLC: A=0x80 => A=0x01, CY=1 */
    cpu.a = 0x80u;
    mem_write(0x0011u, 0x07u);
    cpu.pc = 0x0011u;
    cpu_step(&cpu);
    assert_u8("RLC A", 0x01u, cpu.a);
    if ((cpu.flags & FLAG_CY) == 0u) {
        fprintf(stderr, "FAIL: RLC expected CY\n");
        return 1;
    }

    /* CMA then STC/CMC */
    cpu.a = 0x00u;
    mem_write(0x0012u, 0x2Fu);
    cpu.pc = 0x0012u;
    cpu_step(&cpu);
    assert_u8("CMA", 0xFFu, cpu.a);
    mem_write(0x0013u, 0x37u);
    cpu.pc = 0x0013u;
    cpu_step(&cpu);
    if ((cpu.flags & FLAG_CY) == 0u) {
        fprintf(stderr, "FAIL: STC\n");
        return 1;
    }
    mem_write(0x0014u, 0x3Fu);
    cpu.pc = 0x0014u;
    cpu_step(&cpu);
    if ((cpu.flags & FLAG_CY) != 0u) {
        fprintf(stderr, "FAIL: CMC\n");
        return 1;
    }

    puts("PASS: test_cpu_logic");
    return 0;
}
