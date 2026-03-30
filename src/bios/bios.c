#include "bios.h"

#define BIOS_OUT_CAPACITY 1024

static char g_out[BIOS_OUT_CAPACITY];
static unsigned int g_head = 0;
static unsigned int g_tail = 0;

void bios_init(void) {
    g_head = 0;
    g_tail = 0;
}

void bios_dispatch(cpu_t *cpu) {
    (void)cpu;
}

int bios_pending_output(void) {
    return g_head != g_tail;
}

char bios_get_output(void) {
    char ch = '\0';
    if (g_head != g_tail) {
        ch = g_out[g_tail];
        g_tail = (g_tail + 1u) % BIOS_OUT_CAPACITY;
    }
    return ch;
}
