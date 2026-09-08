#ifndef TURINGOS_KERNEL_H
#define TURINGOS_KERNEL_H
/* The finite state control. Owns `state`; the only place transitions happen. */
#include "../emu/cpu.h"
#include "../tos.h"
#include <stdint.h>

typedef enum {
    KS_BOOT = 0,
    KS_IDLE = 1,      /* parked: a syscall is waiting for console input */
    KS_SHELL = 2,
    KS_RUNNING = 3,
    KS_SYSCALL = 4,
    KS_HALT = 5
} kernel_state_t;

typedef enum {
    KSTOP_BUDGET = 0,       /* max_steps executed */
    KSTOP_HALT = 1,         /* machine is in KS_HALT */
    KSTOP_WAIT_INPUT = 2,   /* parked in KS_IDLE; call again once input is available */
    KSTOP_VSYNC = 3,        /* program requested a frame; host should render then call again */
    KSTOP_BREAKPOINT = 4    /* a breakpoint fired (k->bp_hit is its index) */
} kernel_stop_t;

typedef struct {
    uint8_t  tapes;         /* 1 | 2 | 4 */
    uint32_t tape_len;      /* 32768 | 49152 | 65536 */
    uint32_t hz;            /* 0 = unthrottled; used by kernel_run/hal only, kernel_step never sleeps */
    uint8_t  seed;          /* PRNG seed for BIOS RAND */
    uint8_t  input_mode;    /* TOS_INPUT_CONSOLE | TOS_INPUT_KEYS (informational) */
    uint8_t  disks;         /* 1 | 2 */
    uint8_t  trace;         /* 0 | 1 trace ring enabled */
    uint32_t snap_interval; /* steps between automatic snapshots; 0 = never */
} kernel_config_t;

typedef enum { KBP_PC = 0, KBP_READ = 1, KBP_WRITE = 2, KBP_SYSCALL = 3, KBP_STATE = 4 } kernel_bp_kind_t;
typedef struct { uint8_t kind; uint8_t active; uint16_t lo; uint16_t hi; } kernel_bp_t;
#define KERNEL_BP_MAX 16u

typedef struct { uint8_t from; uint8_t to; const char *why; } kernel_transition_t;
#define KERNEL_TRANSITION_COUNT 12
/* The frozen transition table (index -> from,to,why):
 *  0 BOOT->SHELL     "shell loaded"
 *  1 SHELL->SYSCALL  "OUT 01"
 *  2 SYSCALL->SHELL  "syscall done"
 *  3 SYSCALL->RUNNING "program loaded / syscall done"
 *  4 RUNNING->SYSCALL "OUT 01"
 *  5 RUNNING->SHELL  "program HLT"
 *  6 SYSCALL->IDLE   "waiting for input"
 *  7 IDLE->SYSCALL   "input available"
 *  8 IDLE->HALT      "console EOF"
 *  9 SHELL->HALT     "halt command or tape fault"
 * 10 RUNNING->HALT   "tape fault"
 * 11 SYSCALL->HALT   "console EOF"                                             */

typedef struct {
    kernel_state_t  state;
    cpu_t           cpu;
    uint64_t        steps;
    uint32_t        tick;              /* number of kernel_step calls */
    kernel_config_t cfg;
    kernel_state_t  resume_state;      /* where SYSCALL returns to */
    uint8_t         halt_reason;       /* TOS_HALT_* */
    uint8_t         keys;
    uint8_t         last_stop;         /* kernel_stop_t of the last kernel_step */
    uint8_t         last_syscall;
    uint32_t        frame;             /* VSYNC counter */
    uint32_t        transition_counts[KERNEL_TRANSITION_COUNT];
    kernel_bp_t     bps[KERNEL_BP_MAX];
    int             bp_hit;            /* index of the breakpoint that fired last, -1 if none */
    uint16_t        sp_init;           /* TOS_STACK_TOP(tape_len) */
    uint32_t        last_snapshot_step;
} kernel_t;

void            kernel_config_default(kernel_config_t *cfg);   /* 1 tape, 64K, hz 0, seed 1, console, 1 disk, trace 0, snap 1000 */
void            kernel_init(kernel_t *k, const kernel_config_t *cfg);  /* init mem/bios/fs/trace/snapshot; performs BOOT -> SHELL; no CPU steps */
void            kernel_reset(kernel_t *k);                              /* same as kernel_init with k->cfg */
kernel_stop_t   kernel_step(kernel_t *k, uint32_t max_steps, uint32_t *steps_run);
void            kernel_run(kernel_t *k);                                /* loop until KS_HALT (sleeps on WAIT_INPUT / VSYNC via hal) */
kernel_state_t  kernel_state(const kernel_t *k);
void            kernel_set_keys(kernel_t *k, uint8_t mask);
void            kernel_load_com(kernel_t *k, const uint8_t *bytes, uint32_t len);  /* copy into TPA, PC=0x0100, SP=sp_init, state RUNNING, resume_state RUNNING */
const kernel_transition_t *kernel_transitions(int *count);
int             kernel_bp_add(kernel_t *k, kernel_bp_kind_t kind, uint16_t lo, uint16_t hi);  /* id 0..15 or -1 */
void            kernel_bp_clear(kernel_t *k);
void            kernel_write_meta(kernel_t *k);                         /* refresh the metadata block now */

#endif
