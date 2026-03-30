#include "../../src/emu/mem.h"

#include <stdio.h>
#include <stdlib.h>

static void assert_eq_u8(uint8_t expected, uint8_t actual, const char *label) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%02X actual=0x%02X\n", label, expected, actual);
        exit(1);
    }
}

int main(void) {
    mem_init();

    assert_eq_u8(0x00u, mem_read((addr_t)0x0000u), "mem_init zero @0x0000");
    assert_eq_u8(0x00u, mem_read((addr_t)0x7FFFu), "mem_init zero @0x7FFF");
    assert_eq_u8(0x00u, mem_read((addr_t)0xFFFFu), "mem_init zero @0xFFFF");

    mem_write((addr_t)0x0000u, 0x12u);
    mem_write((addr_t)0x7FFFu, 0xABu);
    mem_write((addr_t)0xFFFFu, 0xFEu);

    assert_eq_u8(0x12u, mem_read((addr_t)0x0000u), "round-trip @0x0000");
    assert_eq_u8(0xABu, mem_read((addr_t)0x7FFFu), "round-trip @0x7FFF");
    assert_eq_u8(0xFEu, mem_read((addr_t)0xFFFFu), "round-trip @0xFFFF");

    uint8_t *raw = mem_raw();
    if (raw == NULL) {
        fprintf(stderr, "FAIL: mem_raw returned NULL\n");
        return 1;
    }
    assert_eq_u8(0x12u, raw[0x0000u], "raw view @0x0000");
    assert_eq_u8(0xABu, raw[0x7FFFu], "raw view @0x7FFF");
    assert_eq_u8(0xFEu, raw[0xFFFFu], "raw view @0xFFFF");

    puts("PASS: test_mem");
    return 0;
}
