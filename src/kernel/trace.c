/* TuringOS v2 — trace ring. A fixed-size ring of packed 8-byte events; no heap.
 * `head` is the total number of pushes ever made (monotonic); the slot of the next write is
 * head % TRACE_CAP and the newest event is ring[(head - 1) % TRACE_CAP]. */
#include "trace.h"

#include <string.h>

static trace_event_t g_ring[TRACE_CAP];
static uint32_t      g_head;      /* total pushes since trace_reset */
static uint32_t      g_count;     /* valid entries, saturates at TRACE_CAP */
static int           g_enabled;   /* not touched by trace_reset: the kernel enables it explicitly */

void trace_reset(void)
{
    memset(g_ring, 0, sizeof(g_ring));
    g_head = 0u;
    g_count = 0u;
}

void trace_enable(int on)
{
    g_enabled = on ? 1 : 0;
}

int trace_enabled(void)
{
    return g_enabled;
}

void trace_push(uint32_t step, uint16_t addr, uint8_t kind, uint8_t value)
{
    trace_event_t *ev;

    if (!g_enabled) {
        return;
    }
    ev = &g_ring[g_head % TRACE_CAP];
    ev->step = step;
    ev->addr = addr;
    ev->kind = kind;
    ev->value = value;
    g_head++;
    if (g_count < TRACE_CAP) {
        g_count++;
    }
}

const trace_event_t *trace_ring(void)
{
    return g_ring;
}

uint32_t trace_head(void)
{
    return g_head;
}

uint32_t trace_count(void)
{
    return g_count;
}
