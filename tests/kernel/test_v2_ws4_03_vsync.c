/* WS4-03: kernel_step never sleeps, and never paces frames.
 *
 * The pacing lives in the host loop (src/main.c, kernel_run), so with the native display on and
 * fps set to 1 the kernel must still return three VSYNC stops immediately. If the frame wait ever
 * moves back inside kernel_step, this takes seconds instead of milliseconds. */
#define _POSIX_C_SOURCE 200809L

#include "../testfw.h"
#include "api/api.h"
#include "hal/hal.h"

#include <fcntl.h>
#include <unistd.h>

/* MVI A,06 / OUT 01 (BIOS VSYNC) / JMP 0100 */
static const uint8_t VSYNC_LOOP[] = { 0x3E, 0x06, 0xD3, 0x01, 0xC3, 0x00, 0x01 };

static int t_vsync_does_not_pace(void) {
    uint32_t t0;
    uint32_t ms;
    int saved;
    int devnull;
    int i;

    /* The frame drawing goes to fd 1; keep it out of the test log. */
    saved = dup(1);
    devnull = open("/dev/null", O_WRONLY);
    ASSERT(saved >= 0 && devnull >= 0);
    ASSERT(dup2(devnull, 1) >= 0);

    hal_init();
    ASSERT(tos_hal_option("display", "1") == 0);
    ASSERT(tos_hal_option("fps", "1") == 0);          /* one frame per second, if anyone paced */
    ASSERT(tos_create(NULL) == 0);
    ASSERT(tos_load_com(VSYNC_LOOP, (uint32_t)sizeof VSYNC_LOOP) == 0);

    t0 = hal_time_ms();
    for (i = 0; i < 3; ++i) {
        (void)tos_step(1000u);
        ASSERT(tos_stop_reason() == (int)KSTOP_VSYNC);
    }
    ms = hal_time_ms() - t0;

    ASSERT(tos_hal_option("display", "0") == 0);
    hal_shutdown();
    (void)dup2(saved, 1);
    (void)close(saved);
    (void)close(devnull);

    ASSERT(tos_frame() == 3u);
    ASSERT(ms < 200u);                                 /* 3 s if the kernel paced the frames */
    return 0;
}

int main(void) {
    TEST("WS4-03: kernel_step returns VSYNC without waiting for the frame", t_vsync_does_not_pace);
    printf("PASS: test_v2_ws4_03_vsync\n");
    RUN_ALL_TESTS();
}
