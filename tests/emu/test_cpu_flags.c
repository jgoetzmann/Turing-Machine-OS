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

int main(void) {
    cpu_t cpu;
    mem_init();
    cpu_init(&cpu);

    /* ADD boundary: 0xFF + 0x01 = 0x00, Z+CY+AC set, parity even */
    mem_write(0x0000u, 0x3Eu); /* MVI A,0xFF */
    mem_write(0x0001u, 0xFFu);
    mem_write(0x0002u, 0x06u); /* MVI B,0x01 */
    mem_write(0x0003u, 0x01u);
    mem_write(0x0004u, 0x80u); /* ADD B */
    cpu_step(&cpu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("ADD result", 0x00u, cpu.a);
    if ((cpu.flags & FLAG_Z) == 0u || (cpu.flags & FLAG_CY) == 0u ||
        (cpu.flags & FLAG_AC) == 0u || (cpu.flags & FLAG_P) == 0u) {
        fprintf(stderr, "FAIL: ADD boundary flags mismatch\n");
        return 1;
    }

    /* SUB boundary: 0x00 - 0x01 = 0xFF, S+CY+AC set, Z clear */
    mem_write(0x0005u, 0x3Eu); /* MVI A,0x00 */
    mem_write(0x0006u, 0x00u);
    mem_write(0x0007u, 0x06u); /* MVI B,0x01 */
    mem_write(0x0008u, 0x01u);
    mem_write(0x0009u, 0x90u); /* SUB B */
    cpu_step(&cpu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("SUB result", 0xFFu, cpu.a);
    if ((cpu.flags & FLAG_S) == 0u || (cpu.flags & FLAG_CY) == 0u ||
        (cpu.flags & FLAG_AC) == 0u || (cpu.flags & FLAG_Z) != 0u) {
        fprintf(stderr, "FAIL: SUB boundary flags mismatch\n");
        return 1;
    }

    /* AND boundary: AC set, CY clear */
    mem_write(0x000Au, 0x3Eu); /* MVI A,0xF0 */
    mem_write(0x000Bu, 0xF0u);
    mem_write(0x000Cu, 0xE6u); /* ANI 0x0F */
    mem_write(0x000Du, 0x0Fu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("AND result", 0x00u, cpu.a);
    if ((cpu.flags & FLAG_AC) == 0u || (cpu.flags & FLAG_CY) != 0u || (cpu.flags & FLAG_Z) == 0u) {
        fprintf(stderr, "FAIL: AND boundary flags mismatch\n");
        return 1;
    }

    /* OR boundary: AC clear, CY clear, S set for 0x80 */
    mem_write(0x000Eu, 0x3Eu); /* MVI A,0x00 */
    mem_write(0x000Fu, 0x00u);
    mem_write(0x0010u, 0xF6u); /* ORI 0x80 */
    mem_write(0x0011u, 0x80u);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("OR result", 0x80u, cpu.a);
    if ((cpu.flags & FLAG_AC) != 0u || (cpu.flags & FLAG_CY) != 0u || (cpu.flags & FLAG_S) == 0u) {
        fprintf(stderr, "FAIL: OR boundary flags mismatch\n");
        return 1;
    }

    /* XOR boundary: zero result, AC/CY clear */
    mem_write(0x0012u, 0x3Eu); /* MVI A,0xAA */
    mem_write(0x0013u, 0xAAu);
    mem_write(0x0014u, 0xEEu); /* XRI 0xAA */
    mem_write(0x0015u, 0xAAu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    assert_u8("XOR result", 0x00u, cpu.a);
    if ((cpu.flags & FLAG_Z) == 0u || (cpu.flags & FLAG_AC) != 0u || (cpu.flags & FLAG_CY) != 0u) {
        fprintf(stderr, "FAIL: XOR boundary flags mismatch\n");
        return 1;
    }

    puts("PASS: test_cpu_flags");
    return 0;
}
