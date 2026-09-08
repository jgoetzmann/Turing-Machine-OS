#ifndef TURINGOS_MEM_H
#define TURINGOS_MEM_H
/* The tape(s). k tapes of L bytes; addresses in [TOS_BANK_BASE, TOS_BANK_END(L)] are per-tape,
 * every other address resolves to tape 0. */
#include <stdint.h>

typedef uint8_t  tape_t;
typedef uint16_t addr_t;

void      mem_init(uint8_t tapes, uint32_t tape_len);   /* tapes: 1|2|4; tape_len: 32768|49152|65536; zero-fills, resets ages/counters */
uint8_t   mem_read(addr_t addr);                        /* out of range: returns 0xFF and sets fault TOS_HALT_TAPE_FAULT */
void      mem_write(addr_t addr, uint8_t val);          /* out of range: ignored and sets fault */
uint8_t   mem_peek(uint8_t tape, addr_t addr);          /* raw read of a specific tape, no side effects, no fault */
void      mem_poke(uint8_t tape, addr_t addr, uint8_t val); /* raw write, no side effects */
uint8_t  *mem_raw(void);                                /* tape 0 backing array (65536 bytes) */
uint8_t  *mem_tape_raw(uint8_t tape);                   /* backing array of tape n (65536 bytes); only the bank window is meaningful for n > 0 */
uint8_t   mem_tape_count(void);
uint32_t  mem_tape_len(void);
int       mem_select_tape(uint8_t n);                   /* 0 ok; -1 and fault TOS_HALT_BAD_TAPE if n >= count */
uint8_t   mem_selected_tape(void);
uint8_t   mem_fault(void);                              /* 0 or the pending TOS_HALT_* fault reason */
void      mem_clear_fault(void);

/* Observation */
void      mem_set_step(uint32_t step);                  /* step number stamped into ages by subsequent accesses */
uint32_t *mem_write_age(uint8_t tape);                  /* uint32[65536]: step of last write (0 = never) */
uint32_t *mem_read_age(uint8_t tape);                   /* uint32[65536]: step of last read  (0 = never) */
void      mem_dirty_pages(uint8_t out[32], uint32_t since_step); /* bit p set if any byte of page p (any tape) was written at step >= since_step */
void      mem_forget_after(uint8_t tape, uint32_t step);  /* drop read/write ages later than `step` (after a snapshot restore) and re-count written cells */
uint64_t  mem_travel(void);                             /* sum of |addr_i - addr_(i-1)| over all reads+writes since mem_init */
uint64_t  mem_accesses(void);                           /* number of reads+writes since mem_init */
uint32_t  mem_cells_written(void);                      /* distinct (tape,addr) cells with write age != 0 */

#endif
