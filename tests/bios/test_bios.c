#include "../../src/bios/bios.h"
#include "../../src/hal/hal.h"
#include "../../src/emu/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void assert_u8(const char *label, unsigned int expected, unsigned int actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s expected=0x%02X actual=0x%02X\n", label, expected, actual);
        exit(1);
    }
}

int main(void) {
    cpu_t cpu = {0};

    mem_init(1u, 65536u);
    bios_init();
    if (bios_pending_output()) {
        fprintf(stderr, "FAIL: queue should be empty after bios_init\n");
        return 1;
    }
    assert_u8("default disk", 0u, bios_current_disk());
    assert_u8("default track", 0u, bios_current_track());
    assert_u8("default sector", 1u, bios_current_sector());
    assert_u8("default dma high", 0xE0u, (unsigned int)(bios_dma_addr() >> 8));
    assert_u8("default dma low", 0x00u, (unsigned int)(bios_dma_addr() & 0xFFu));

    cpu.a = 0x02u; /* CONOUT */

    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.c = 'A';
    cpu.io_out_pending = 1u;
    bios_dispatch(&cpu);
    if (!bios_pending_output()) {
        fprintf(stderr, "FAIL: CONOUT should queue one character\n");
        return 1;
    }
    assert_u8("CONOUT drain char", 'A', (unsigned char)bios_get_output());
    if (bios_pending_output()) {
        fprintf(stderr, "FAIL: queue should be empty after drain\n");
        return 1;
    }
    assert_u8("dispatch clears io_out_pending", 0u, cpu.io_out_pending);

    /* Repeated enqueue/dequeue validates ring-index wrap behavior. */
    for (unsigned int i = 0; i < 3000u; ++i) {
        unsigned char expected = (unsigned char)('a' + (i % 26u));
        cpu.a = 0x02u;
        cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
        cpu.c = (uint8_t)expected;
        bios_dispatch(&cpu);
        assert_u8("queued character", expected, (unsigned char)bios_get_output());
    }

    /* CONIN should read one char into A. */
    if (hal_con_push((uint8_t)'Q') == EOF) {
        fprintf(stderr, "FAIL: ungetc setup for CONIN failed\n");
        return 1;
    }
    cpu.a = 0x01u; /* CONIN */
    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    bios_dispatch(&cpu);
    assert_u8("CONIN stores char in A", 'Q', cpu.a);

    /* Disk-parameter setup stubs should persist values. */
    cpu.a = 0x09u; /* SELDISK */
    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.c = 0x03u;
    bios_dispatch(&cpu);
    assert_u8("SELDISK value (out-of-range select keeps disk 0)", 0x00u, bios_current_disk());

    cpu.a = 0x0Au; /* SETTRK */

    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.c = 0x11u;
    bios_dispatch(&cpu);
    assert_u8("SETTRK value", 0x11u, bios_current_track());

    cpu.a = 0x0Bu; /* SETSEC */

    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.c = 0x19u;
    bios_dispatch(&cpu);
    assert_u8("SETSEC value", 0x19u, bios_current_sector());

    cpu.a = 0x0Cu; /* SETDMA */

    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.d = 0x12u;
    cpu.e = 0x34u;
    bios_dispatch(&cpu);
    assert_u8("SETDMA high", 0x12u, (unsigned int)(bios_dma_addr() >> 8));
    assert_u8("SETDMA low", 0x34u, (unsigned int)(bios_dma_addr() & 0xFFu));

    /* READ/WRITE delegate to FS hooks; current FS stubs report failure (A=1). */
    cpu.a = 0x0Du; /* READ */
    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.io_out_pending = 1u;
    bios_dispatch(&cpu);
    assert_u8("READ status", 1u, cpu.a);
    assert_u8("READ clears pending", 0u, cpu.io_out_pending);

    cpu.a = 0x0Eu; /* WRITE */

    cpu.io_out_value = cpu.a; cpu.io_out_pending = 1u;
    cpu.io_out_pending = 1u;
    bios_dispatch(&cpu);
    assert_u8("WRITE status", 1u, cpu.a);
    assert_u8("WRITE clears pending", 0u, cpu.io_out_pending);

    puts("PASS: test_bios");
    return 0;
}
