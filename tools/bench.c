/* bench — native emulator throughput (SPEC §S7, `make bench`).
 *
 * Creates a machine through the embedding API with tracing and automatic snapshots off, loads a
 * hand-assembled tight loop into the TPA and runs 20,000,000 instructions, then prints
 *     steps/s=<n>
 * measured with clock(). The loop never does I/O, so the number is the raw kernel_step +
 * cpu_step + mem_read/mem_write cost per instruction, including the age/travel bookkeeping.
 * No pass/fail gate: bench prints numbers only. */
#include "api/api.h"
#include "hal/hal.h"
#include "kernel/kernel.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BENCH_STEPS 20000000UL   /* total instructions to execute */
#define BENCH_CHUNK 1000000u     /* max_steps per tos_step call */

/* 0100: LXI H,0000H
 * 0103: INX H
 * 0104: JMP 0103H */
static const uint8_t loop_com[] = {
    0x21, 0x00, 0x00,
    0x23,
    0xC3, 0x03, 0x01
};

int main(int argc, char **argv)
{
    tos_config_t  cfg;
    unsigned long done = 0;
    clock_t       t0, t1;
    double        secs;
    unsigned long rate;

    kernel_config_default(&cfg);
    cfg.trace = (argc > 1 && strcmp(argv[1], "--trace") == 0) ? 1u : 0u;   /* --trace: ring + ages on */
    cfg.snap_interval = 0;  /* no periodic full-machine snapshots */
    cfg.hz = 0;             /* unthrottled */

    hal_set_option("raw", "0");   /* never put the terminal into raw mode */
    hal_init();

    if (tos_create(&cfg) != 0) {
        fprintf(stderr, "bench: tos_create failed\n");
        hal_shutdown();
        return 1;
    }
    tos_trace_enable((int)cfg.trace);

    if (tos_load_com(loop_com, (uint32_t)sizeof loop_com) != 0) {
        fprintf(stderr, "bench: tos_load_com failed\n");
        hal_shutdown();
        return 1;
    }

    t0 = clock();
    while (done < BENCH_STEPS) {
        uint32_t want = (uint32_t)(BENCH_STEPS - done);
        uint32_t ran;
        if (want > BENCH_CHUNK) want = BENCH_CHUNK;
        ran = tos_step(want);
        done += ran;
        if (ran == 0 || tos_stop_reason() == KSTOP_HALT) {
            fprintf(stderr, "bench: machine stopped early (state=%s stop=%d halt=%u)\n",
                    tos_state_name(tos_state()), tos_stop_reason(), (unsigned)tos_halt_reason());
            break;
        }
    }
    t1 = clock();

    secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    if (secs <= 0.0) secs = 1.0 / (double)CLOCKS_PER_SEC;
    rate = (unsigned long)((double)done / secs);

    printf("steps/s=%lu trace=%s\n", rate, cfg.trace ? "on" : "off");
    printf("steps=%lu elapsed_ms=%lu cycles=%lu\n",
           done,
           (unsigned long)(secs * 1000.0),
           (unsigned long)tos_cycles_lo());

    hal_shutdown();
    return 0;
}
