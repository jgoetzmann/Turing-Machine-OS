#include "../../src/emu/mem.h"
#include "../testfw.h"

#include <stdio.h>

static int test_mem_boundaries(void) {
    mem_init(1u, 65536u);

    ASSERT(mem_read((addr_t)0x0000u) == 0x00u);
    ASSERT(mem_read((addr_t)0x7FFFu) == 0x00u);
    ASSERT(mem_read((addr_t)0xFFFFu) == 0x00u);

    mem_write((addr_t)0x0000u, 0x12u);
    mem_write((addr_t)0x7FFFu, 0xABu);
    mem_write((addr_t)0xFFFFu, 0xFEu);

    ASSERT(mem_read((addr_t)0x0000u) == 0x12u);
    ASSERT(mem_read((addr_t)0x7FFFu) == 0xABu);
    ASSERT(mem_read((addr_t)0xFFFFu) == 0xFEu);

    uint8_t *raw = mem_raw();
    ASSERT(raw != NULL);
    ASSERT(raw[0x0000u] == 0x12u);
    ASSERT(raw[0x7FFFu] == 0xABu);
    ASSERT(raw[0xFFFFu] == 0xFEu);

    return 0;
}

int main(void) {
    TEST("test_mem_boundaries", test_mem_boundaries);
    puts("PASS: test_mem");
    RUN_ALL_TESTS();
}
