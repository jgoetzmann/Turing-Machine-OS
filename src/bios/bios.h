#ifndef TURINGOS_BIOS_H
#define TURINGOS_BIOS_H
#include "../emu/cpu.h"
#include <stdint.h>

#define BIOS_DONE   0
#define BIOS_WAIT   1   /* needs console input; kernel parks in KS_IDLE and calls bios_dispatch again later with the same cpu */
#define BIOS_VSYNC  2   /* frame requested; kernel returns KSTOP_VSYNC after completing the syscall */
#define BIOS_EOF    3   /* console input hit EOF; kernel halts with TOS_HALT_EOF */

void     bios_init(void);
void     bios_reset(void);
void     bios_set_tape_len(uint32_t tape_len);   /* DMA default = TOS_DMA_DEFAULT(L) */
void     bios_set_seed(uint8_t seed);
int      bios_dispatch(cpu_t *cpu);              /* function id = cpu->io_out_value (== A at OUT time); returns BIOS_* ; clears cpu->io_out_pending only when not BIOS_WAIT */
int      bios_run_program_pending(void);         /* 1 once after a successful RUN; reading clears it */
int      bios_pending_output(void);
char     bios_get_output(void);
uint8_t  bios_current_disk(void);
uint8_t  bios_current_track(void);
uint8_t  bios_current_sector(void);
uint16_t bios_dma_addr(void);
uint8_t  bios_rand(void);                        /* advances the PRNG (8-bit xorshift: x^=x<<3; x^=x>>5; x^=x<<1 on a uint8) */
uint32_t bios_ticks(void);                       /* frames completed */
void     bios_tick(void);                        /* kernel calls after each VSYNC */
uint8_t  bios_last_fn(void);
uint32_t bios_state_size(void);
void     bios_state_save(uint8_t *buf);
void     bios_state_load(const uint8_t *buf);
#endif
