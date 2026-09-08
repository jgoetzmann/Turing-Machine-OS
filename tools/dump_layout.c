/* dump_layout — prints build/gen/layout.json (SPEC §S9, WS2-03): byte offsets and sizes of the
 * frozen structs that the web engine reads straight out of wasm memory through tos_cpu_ptr(),
 * tos_trace_ptr() and the kernel_config_t it hands to tos_create().
 *
 * Output (keys in exactly this order; whitespace is not significant):
 * {"cpu":{"size":296,"a":0,...,"cycles":288},
 *  "trace_event":{"size":8,"step":0,"addr":4,"kind":6,"value":7},
 *  "config":{"size":20,"tapes":0,...,"snap_interval":16}}
 * The offsets are identical on x86-64, arm64 and wasm32 because every field is naturally
 * aligned and uint64_t has 8-byte alignment on all three. */
#include "emu/cpu.h"
#include "kernel/kernel.h"
#include "kernel/trace.h"
#include <stddef.h>
#include <stdio.h>

#define SZ(T)     ((unsigned long)sizeof(T))
#define OFF(T, f) ((unsigned long)offsetof(T, f))

static void field(const char *name, unsigned long value, int last)
{
    printf("\"%s\":%lu%s", name, value, last ? "" : ",");
}

int main(void)
{
    /* cpu_t */
    printf("{\"cpu\":{");
    field("size", SZ(cpu_t), 0);
    field("a", OFF(cpu_t, a), 0);
    field("b", OFF(cpu_t, b), 0);
    field("c", OFF(cpu_t, c), 0);
    field("d", OFF(cpu_t, d), 0);
    field("e", OFF(cpu_t, e), 0);
    field("h", OFF(cpu_t, h), 0);
    field("l", OFF(cpu_t, l), 0);
    field("sp", OFF(cpu_t, sp), 0);
    field("pc", OFF(cpu_t, pc), 0);
    field("flags", OFF(cpu_t, flags), 0);
    field("halted", OFF(cpu_t, halted), 0);
    field("io_out_pending", OFF(cpu_t, io_out_pending), 0);
    field("io_out_port", OFF(cpu_t, io_out_port), 0);
    field("io_out_value", OFF(cpu_t, io_out_value), 0);
    field("io_in_ports", OFF(cpu_t, io_in_ports), 0);
    field("interrupts_enabled", OFF(cpu_t, interrupts_enabled), 0);
    field("rim_value", OFF(cpu_t, rim_value), 0);
    field("sim_value", OFF(cpu_t, sim_value), 0);
    field("cycles", OFF(cpu_t, cycles), 1);
    printf("},\n");

    /* trace_event_t */
    printf(" \"trace_event\":{");
    field("size", SZ(trace_event_t), 0);
    field("step", OFF(trace_event_t, step), 0);
    field("addr", OFF(trace_event_t, addr), 0);
    field("kind", OFF(trace_event_t, kind), 0);
    field("value", OFF(trace_event_t, value), 1);
    printf("},\n");

    /* kernel_config_t (== tos_config_t) */
    printf(" \"config\":{");
    field("size", SZ(kernel_config_t), 0);
    field("tapes", OFF(kernel_config_t, tapes), 0);
    field("tape_len", OFF(kernel_config_t, tape_len), 0);
    field("hz", OFF(kernel_config_t, hz), 0);
    field("seed", OFF(kernel_config_t, seed), 0);
    field("input_mode", OFF(kernel_config_t, input_mode), 0);
    field("disks", OFF(kernel_config_t, disks), 0);
    field("trace", OFF(kernel_config_t, trace), 0);
    field("snap_interval", OFF(kernel_config_t, snap_interval), 1);
    printf("}}\n");

    return 0;
}
