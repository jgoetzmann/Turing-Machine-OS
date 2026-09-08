#ifndef TURINGOS_TRACE_H
#define TURINGOS_TRACE_H
#include <stdint.h>
/* Fixed-size ring of packed 8-byte events. No heap. */
#define TR_FETCH   0u   /* addr = pc, value = opcode            (pushed by kernel per instruction) */
#define TR_READ    1u   /* addr, value read                      (pushed by mem_read)  */
#define TR_WRITE   2u   /* addr, value written                   (pushed by mem_write) */
#define TR_SYSCALL 3u   /* addr = function id, value = C reg     (pushed by kernel) */
#define TR_STATE   4u   /* addr = (from << 8) | to, value = transition index (pushed by kernel) */
#define TR_TAPE    5u   /* addr = selected tape                  (pushed by kernel) */
#define TRACE_CAP  65536u

typedef struct { uint32_t step; uint16_t addr; uint8_t kind; uint8_t value; } trace_event_t;

void                 trace_reset(void);
void                 trace_enable(int on);
int                  trace_enabled(void);
void                 trace_push(uint32_t step, uint16_t addr, uint8_t kind, uint8_t value);  /* no-op when disabled */
const trace_event_t *trace_ring(void);        /* TRACE_CAP entries */
uint32_t             trace_head(void);        /* index of the next slot to write (monotonic count mod nothing: total pushes) */
uint32_t             trace_count(void);       /* valid entries, <= TRACE_CAP; the newest is ring[(head-1) % TRACE_CAP] */
#endif
