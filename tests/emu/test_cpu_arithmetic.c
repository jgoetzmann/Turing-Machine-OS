#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

#define FLAG_Z  0x40u
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

    /* MVI A,0x05 ; MVI B,0x03 ; ADD B => A=0x08 */
    mem_write(0x0000u, 0x3Eu);
    mem_write(0x0001u, 0x05u);
    mem_write(0x0002u, 0x06u);
    mem_write(0x0003u, 0x03u);
    mem_write(0x0004u, 0x80u);
    cpu_step(&cpu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("ADD B", 0x08u, cpu.a);

    /* ADI 0xF8 => 0x00 with carry */
    mem_write(0x0005u, 0xC6u);
    mem_write(0x0006u, 0xF8u);
    cpu_step(&cpu);
    assert_u8("ADI wraps", 0x00u, cpu.a);
    if ((cpu.flags & FLAG_Z) == 0u || (cpu.flags & FLAG_CY) == 0u) {
        fprintf(stderr, "FAIL: ADI flags expected Z and CY set\n");
        return 1;
    }

    /* SUI 0x01 => 0xFF with carry (borrow) */
    mem_write(0x0007u, 0xD6u);
    mem_write(0x0008u, 0x01u);
    cpu_step(&cpu);
    assert_u8("SUI", 0xFFu, cpu.a);
    if ((cpu.flags & FLAG_CY) == 0u) {
        fprintf(stderr, "FAIL: SUI expected CY set\n");
        return 1;
    }

    /* INR C / DCR C */
    cpu.c = 0x0Fu;
    mem_write(0x0009u, 0x0Cu); /* INR C */
    mem_write(0x000Au, 0x0Du); /* DCR C */
    cpu_step(&cpu);
    assert_u8("INR C", 0x10u, cpu.c);
    cpu_step(&cpu);
    assert_u8("DCR C", 0x0Fu, cpu.c);

    /* INX H / DCX H */
    cpu.h = 0x12u;
    cpu.l = 0xFFu;
    mem_write(0x000Bu, 0x23u); /* INX H => 0x1300 */
    mem_write(0x000Cu, 0x2Bu); /* DCX H => 0x12FF */
    cpu_step(&cpu);
    assert_u16("INX H", 0x1300u, (uint16_t)(((uint16_t)cpu.h << 8u) | cpu.l));
    cpu_step(&cpu);
    assert_u16("DCX H", 0x12FFu, (uint16_t)(((uint16_t)cpu.h << 8u) | cpu.l));

    /* DAD B: HL=0x12FF, BC=0x0101 => 0x1400 */
    cpu.b = 0x01u;
    cpu.c = 0x01u;
    mem_write(0x000Du, 0x09u);
    cpu_step(&cpu);
    assert_u16("DAD B", 0x1400u, (uint16_t)(((uint16_t)cpu.h << 8u) | cpu.l));

    /* DAA sanity: 0x09 + 0x09 = 0x12, DAA => 0x18 */
    cpu.a = 0x09u;
    mem_write(0x000Eu, 0xC6u);
    mem_write(0x000Fu, 0x09u);
    mem_write(0x0010u, 0x27u);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("DAA", 0x18u, cpu.a);

    puts("PASS: test_cpu_arithmetic");
    return 0;
}
