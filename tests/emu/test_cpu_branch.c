#include "../../src/emu/cpu.h"
#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

#define FLAG_Z 0x40u

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

    /* JMP 0x1234 */
    mem_write(0x0000u, 0xC3u);
    mem_write(0x0001u, 0x34u);
    mem_write(0x0002u, 0x12u);
    cpu_step(&cpu);
    assert_u16("JMP", 0x1234u, cpu.pc);

    /* JZ taken */
    cpu.flags = FLAG_Z;
    cpu.pc = 0x0010u;
    mem_write(0x0010u, 0xCAu);
    mem_write(0x0011u, 0x78u);
    mem_write(0x0012u, 0x56u);
    cpu_step(&cpu);
    assert_u16("JZ taken", 0x5678u, cpu.pc);

    /* JNZ not taken */
    cpu.flags = FLAG_Z;
    cpu.pc = 0x0020u;
    mem_write(0x0020u, 0xC2u);
    mem_write(0x0021u, 0xEFu);
    mem_write(0x0022u, 0xBEu);
    cpu_step(&cpu);
    assert_u16("JNZ not taken", 0x0023u, cpu.pc);

    /* CALL + RET */
    cpu.pc = 0x0030u;
    cpu.sp = 0x2000u;
    mem_write(0x0030u, 0xCDu);
    mem_write(0x0031u, 0x00u);
    mem_write(0x0032u, 0x40u);
    cpu_step(&cpu);
    assert_u16("CALL target", 0x4000u, cpu.pc);
    assert_u16("CALL SP", 0x1FFEu, cpu.sp);
    assert_u16("CALL return pushed", 0x0033u, (uint16_t)(mem_read(0x1FFEu) | ((uint16_t)mem_read(0x1FFFu) << 8u)));

    mem_write(0x4000u, 0xC9u); /* RET */
    cpu_step(&cpu);
    assert_u16("RET pc", 0x0033u, cpu.pc);
    assert_u16("RET SP", 0x2000u, cpu.sp);

    /* CZ taken */
    cpu.flags = FLAG_Z;
    cpu.pc = 0x0050u;
    cpu.sp = 0x2200u;
    mem_write(0x0050u, 0xCCu);
    mem_write(0x0051u, 0x22u);
    mem_write(0x0052u, 0x22u);
    cpu_step(&cpu);
    assert_u16("CZ target", 0x2222u, cpu.pc);
    assert_u16("CZ SP", 0x21FEu, cpu.sp);

    /* RNZ not taken with Z set */
    cpu.flags = FLAG_Z;
    cpu.pc = 0x0060u;
    mem_write(0x0060u, 0xC0u);
    cpu_step(&cpu);
    assert_u16("RNZ not taken", 0x0061u, cpu.pc);

    puts("PASS: test_cpu_branch");
    return 0;
}
