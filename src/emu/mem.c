#include "mem.h"

#include <string.h>

static uint8_t g_mem[65536];

void mem_init(void) {
    memset(g_mem, 0, sizeof(g_mem));
}

uint8_t mem_read(addr_t addr) {
    return g_mem[addr];
}

void mem_write(addr_t addr, uint8_t val) {
    g_mem[addr] = val;
}

uint8_t *mem_raw(void) {
    return g_mem;
}
