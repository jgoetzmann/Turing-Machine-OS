/* The BIOS console-output ring: CONOUT (0x02) fills it, bios_pending_output / bios_get_output
 * drain it in order, and an overflow hands the oldest byte to the host rather than dropping it. */
#define _POSIX_C_SOURCE 200809L

#include "../../src/bios/bios.h"
#include "../../src/emu/mem.h"
#include "../../src/hal/hal.h"
#include "../../src/tos.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RING_CAP 4096

static void fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
}

static void expect_u32(const char *what, unsigned long want, unsigned long got) {
    if (want != got) {
        fprintf(stderr, "FAIL: %s: expected %lu, got %lu\n", what, want, got);
        exit(1);
    }
}

/* One CONOUT syscall with C = ch. */
static void conout(cpu_t *cpu, uint8_t ch) {
    cpu->a = TOS_BIOS_CONOUT;
    cpu->c = ch;
    cpu->io_out_value = cpu->a;
    cpu->io_out_pending = 1u;
    bios_dispatch(cpu);
}

int main(void) {
    cpu_t cpu;
    int i;
    int saved;
    int devnull;

    mem_init(1u, 65536u);
    bios_init();
    memset(&cpu, 0, sizeof cpu);

    if (bios_pending_output()) fail("a fresh BIOS has pending output");

    conout(&cpu, (uint8_t)'A');
    expect_u32("io_out_pending cleared", 0u, cpu.io_out_pending);
    if (!bios_pending_output()) fail("CONOUT produced no output");
    expect_u32("the byte that came back", (unsigned long)'A', (unsigned long)(unsigned char)bios_get_output());
    if (bios_pending_output()) fail("the ring still has output after draining one byte");

    /* Order is preserved across a long run of bytes. */
    for (i = 0; i < 100; ++i) conout(&cpu, (uint8_t)('a' + (i % 26)));
    for (i = 0; i < 100; ++i) {
        if (!bios_pending_output()) fail("the ring ran dry early");
        expect_u32("byte order", (unsigned long)('a' + (i % 26)), (unsigned long)(unsigned char)bios_get_output());
    }
    if (bios_pending_output()) fail("the ring kept a byte nobody wrote");

    /* Exactly one ring's worth fits without spilling. */
    for (i = 0; i < RING_CAP; ++i) conout(&cpu, (uint8_t)(i & 0xFF));
    for (i = 0; i < RING_CAP; ++i) expect_u32("full-ring byte", (unsigned long)(i & 0xFF),
                                              (unsigned long)(unsigned char)bios_get_output());
    if (bios_pending_output()) fail("more bytes than the ring holds came back");

    /* One byte too many: the oldest goes straight to the host, and the ring keeps the rest in
       order. The host write is real, so send fd 1 to /dev/null while it happens. */
    saved = dup(1);
    devnull = open("/dev/null", O_WRONLY);
    if (saved < 0 || devnull < 0 || dup2(devnull, 1) < 0) fail("could not redirect stdout");
    for (i = 0; i < RING_CAP + 1; ++i) conout(&cpu, (uint8_t)(i & 0xFF));
    (void)dup2(saved, 1);
    (void)close(saved);
    (void)close(devnull);

    for (i = 1; i < RING_CAP + 1; ++i) {
        if (!bios_pending_output()) fail("the ring lost a byte on overflow");
        expect_u32("post-overflow byte", (unsigned long)(i & 0xFF), (unsigned long)(unsigned char)bios_get_output());
    }
    if (bios_pending_output()) fail("the ring held more than its capacity after an overflow");

    puts("PASS: test_conout");
    return 0;
}
