/* TuringOS v2 — the finite state control. Owns `state`; the only place transitions happen.
 * Implements the frozen 12-entry transition table in kernel.h and the loop in SPEC §S2.
 * No stdio, no heap, no clock: kernel_step never sleeps and never reads time. */
#include "kernel.h"
#include "snapshot.h"
#include "trace.h"

#include "../bios/bios.h"
#include "../emu/cpu.h"
#include "../emu/mem.h"
#include "../fs/fs.h"
#include "../hal/hal.h"
#include "../tos.h"

#include <stddef.h>
#include <string.h>

/* ---- frozen transition table ------------------------------------------- */
static const kernel_transition_t k_table[KERNEL_TRANSITION_COUNT] = {
    { KS_BOOT,    KS_SHELL,   "shell loaded" },                    /* 0 */
    { KS_SHELL,   KS_SYSCALL, "OUT 01" },                          /* 1 */
    { KS_SYSCALL, KS_SHELL,   "syscall done" },                    /* 2 */
    { KS_SYSCALL, KS_RUNNING, "program loaded / syscall done" },   /* 3 */
    { KS_RUNNING, KS_SYSCALL, "OUT 01" },                          /* 4 */
    { KS_RUNNING, KS_SHELL,   "program HLT" },                     /* 5 */
    { KS_SYSCALL, KS_IDLE,    "waiting for input" },               /* 6 */
    { KS_IDLE,    KS_SYSCALL, "input available" },                 /* 7 */
    { KS_IDLE,    KS_HALT,    "console EOF" },                     /* 8 */
    { KS_SHELL,   KS_HALT,    "halt command or tape fault" },      /* 9 */
    { KS_RUNNING, KS_HALT,    "tape fault" },                      /* 10 */
    { KS_SYSCALL, KS_HALT,    "console EOF" }                      /* 11 */
};

/* ---- module state (host-side, never part of the machine) ---------------- */

/* Console-output hook. The embedding API installs one so it can capture the bytes the kernel
 * drains out of the BIOS ring; when none is installed the bytes go straight to hal_con_out. */
static void (*g_con_out)(uint8_t ch);

/* Every write stamped with an age >= this value happened during the current kernel_step call
 * (feeds TOS_META_DIRTY). */
static uint32_t g_dirty_since = 1u;

/* hal_keys() sampled once per kernel_step call (one host call per call, not per instruction). */
static uint8_t g_hal_keys;

/* The mask presented on IN 0x03: hal keys OR the host-set mask in k->keys. */
static uint8_t g_port_keys;

/* Not in kernel.h: the API links against this to route console output. */
void kernel_set_con_out(void (*fn)(uint8_t ch));

void kernel_set_con_out(void (*fn)(uint8_t ch))
{
    g_con_out = fn;
}

/* ---- helpers ------------------------------------------------------------ */

static uint8_t k_tape_for(uint16_t addr)
{
    uint32_t L = mem_tape_len();
    if ((uint32_t)addr >= (uint32_t)TOS_BANK_BASE && (uint32_t)addr <= TOS_BANK_END(L)) {
        return mem_selected_tape();
    }
    return 0u;
}

static void k_drain_output(void)
{
    while (bios_pending_output()) {
        uint8_t ch = (uint8_t)bios_get_output();
        if (g_con_out != NULL) {
            g_con_out(ch);
        } else {
            hal_con_out(ch);
        }
    }
}

/* Copy the shell image into the TPA (raw pokes: the boot does not age any cell). */
static void k_load_shell(void)
{
    const uint8_t *blob = NULL;
    uint32_t len = 0u;
    uint32_t i;

    if (hal_shell_blob(&blob, &len) != 0 || blob == NULL || len == 0u) {
        /* No shell available: a lone HLT in SHELL state halts the machine with TOS_HALT_COMMAND. */
        mem_poke(0u, (addr_t)TOS_TPA_BASE, 0x76u);
        return;
    }
    if (len > TOS_TPA_SIZE) {
        len = TOS_TPA_SIZE;
    }
    for (i = 0u; i < len; i++) {
        mem_poke(0u, (addr_t)(TOS_TPA_BASE + i), blob[i]);
    }
}

/* Put the CPU at the start of the shell: PC = 0x0100, SP = sp_init, tape 0 selected. */
static void k_enter_shell_cpu(kernel_t *k)
{
    /* The cycle odometer counts the machine's lifetime, not one program's: a program returning to
       the shell must not rewind it, or the --hz throttle loses its reference point. */
    const uint64_t cycles = k->cpu.cycles;

    cpu_reset(&k->cpu);
    k->cpu.cycles = cycles;
    k->cpu.pc = (uint16_t)TOS_TPA_BASE;
    k->cpu.sp = k->sp_init;
    k->cpu.halted = 0;
    k->cpu.io_out_pending = 0u;
    mem_clear_fault();
    (void)mem_select_tape(0u);
    mem_clear_fault();
}

static int k_bp_find(const kernel_t *k, kernel_bp_kind_t kind, uint16_t v)
{
    uint32_t i;
    for (i = 0u; i < KERNEL_BP_MAX; i++) {
        const kernel_bp_t *b = &k->bps[i];
        if ((b->active & 1u) == 0u || b->kind != (uint8_t)kind) {
            continue;
        }
        if (v >= b->lo && v <= b->hi) {
            return (int)i;
        }
    }
    return -1;
}

/* PC breakpoints are edge-triggered: bit 1 of `active` remembers "already fired for this visit",
 * so a hit does not re-fire until the PC leaves the range and comes back. */
static int k_bp_pc(kernel_t *k, uint16_t pc)
{
    int hit = -1;
    uint32_t i;
    for (i = 0u; i < KERNEL_BP_MAX; i++) {
        kernel_bp_t *b = &k->bps[i];
        int inside;
        if ((b->active & 1u) == 0u || b->kind != (uint8_t)KBP_PC) {
            continue;
        }
        inside = (pc >= b->lo && pc <= b->hi);
        if (!inside) {
            b->active = (uint8_t)(b->active & (uint8_t)~2u);
            continue;
        }
        if (b->active & 2u) {
            continue;
        }
        b->active = (uint8_t)(b->active | 2u);
        if (hit < 0) {
            hit = (int)i;
        }
    }
    return hit;
}

/* After the instruction stamped `stamp`: did it read/write an address inside a READ/WRITE range?
 * Uses the mem age arrays (the stamp of the last access to every cell on every tape). */
/* The step number kernel.c last stamped into the memory ages; mem.h has no getter for it. */
static uint32_t g_step_stamp = 0u;

static void k_set_step(uint32_t stamp)
{
    g_step_stamp = stamp;
    mem_set_step(stamp);
}

static int k_bp_access(const kernel_t *k, uint32_t stamp)
{
    uint8_t tapes = mem_tape_count();
    uint32_t i;
    for (i = 0u; i < KERNEL_BP_MAX; i++) {
        const kernel_bp_t *b = &k->bps[i];
        uint8_t t;
        if ((b->active & 1u) == 0u) {
            continue;
        }
        if (b->kind != (uint8_t)KBP_READ && b->kind != (uint8_t)KBP_WRITE) {
            continue;
        }
        for (t = 0u; t < tapes; t++) {
            const uint32_t *age = (b->kind == (uint8_t)KBP_READ) ? mem_read_age(t) : mem_write_age(t);
            uint32_t a;
            if (age == NULL) {
                continue;
            }
            for (a = b->lo; a <= (uint32_t)b->hi; a++) {
                if (age[a] == stamp) {
                    return (int)i;
                }
            }
        }
    }
    return -1;
}

/* Watchpoints for the tape accesses a BIOS call made itself (sector DMA, program loading).
 * They carry the step stamp kernel_step set on entry, the same one the instruction arm uses. */
static int k_bp_syscall_access(kernel_t *k)
{
    int hit = k_bp_access(k, g_step_stamp);

    if (hit >= 0) {
        k->bp_hit = hit;
    }
    return hit;
}

/* Perform transition `idx`: assign state, count it, trace it. Returns 1 when a KBP_STATE
 * breakpoint fired on the destination state. */
static int k_transit(kernel_t *k, int idx)
{
    const kernel_transition_t *t = &k_table[idx];
    uint8_t from = (uint8_t)k->state;
    int hit;

    k->state = (kernel_state_t)t->to;
    k->transition_counts[idx]++;
    if (trace_enabled()) {
        trace_push((uint32_t)k->steps, (uint16_t)(((uint16_t)from << 8) | (uint16_t)t->to),
                   (uint8_t)TR_STATE, (uint8_t)idx);
    }
    hit = k_bp_find(k, KBP_STATE, (uint16_t)t->to);
    if (hit >= 0) {
        k->bp_hit = hit;
        return 1;
    }
    return 0;
}

/* Halt from SHELL (transition 9) or RUNNING (transition 10) with `reason`. */
static int k_halt_from_exec(kernel_t *k, uint8_t reason)
{
    k->halt_reason = reason;
    k->cpu.io_out_pending = 0u;
    mem_clear_fault();
    return k_transit(k, k->state == KS_SHELL ? 9 : 10);
}

static void k_refresh_ports(kernel_t *k)
{
    uint32_t L = mem_tape_len();
    g_port_keys = (uint8_t)(g_hal_keys | k->keys);
    k->cpu.io_in_ports[TOS_PORT_TAPE] = mem_selected_tape();
    k->cpu.io_in_ports[TOS_PORT_KEYS] = g_port_keys;
    k->cpu.io_in_ports[TOS_PORT_TAPES] = mem_tape_count();
    k->cpu.io_in_ports[TOS_PORT_PAGES] = (uint8_t)((L / 256u) & 0xFFu);
}

/* Dispatch the pending BIOS call. Returns the BIOS_* result; sets *bp when a KBP_SYSCALL fired. */
static int k_dispatch(kernel_t *k, int *bp)
{
    uint8_t fn = k->cpu.io_out_value;
    uint8_t c = k->cpu.c;
    int r;
    int hit;

    k_refresh_ports(k);
    r = bios_dispatch(&k->cpu);
    k->last_syscall = fn;
    if (trace_enabled()) {
        trace_push((uint32_t)k->steps, (uint16_t)fn, (uint8_t)TR_SYSCALL, c);
    }
    hit = k_bp_find(k, KBP_SYSCALL, (uint16_t)fn);
    if (hit >= 0) {
        k->bp_hit = hit;
        *bp = 1;
    }
    return r;
}

/* Handle a BIOS_DONE / BIOS_WAIT / BIOS_VSYNC result while in SYSCALL state.
 * Returns a kernel_stop_t to return with, or -1 to keep going. */
static int k_after_dispatch(kernel_t *k, int r, int *bp)
{
    uint32_t L = mem_tape_len();

    if (r == BIOS_WAIT) {
        if (k_transit(k, 6)) {                     /* SYSCALL -> IDLE */
            *bp = 1;
        }
        return (int)KSTOP_WAIT_INPUT;
    }
    k->cpu.io_out_pending = 0u;
    if (r == BIOS_VSYNC) {
        k->frame++;
        bios_tick();
        hal_display(mem_raw() + TOS_DISPLAY_BASE(L));
        /* Frame pacing is the host loop's job: kernel_step returns KSTOP_VSYNC and never sleeps
           (CLAUDE.md, SPEC WS4-03). See the KSTOP_VSYNC arm in src/main.c and kernel_run below. */
        if (k_transit(k, k->resume_state == KS_RUNNING ? 3 : 2)) {
            *bp = 1;
        }
        return (int)KSTOP_VSYNC;
    }
    /* BIOS_DONE (anything unknown is treated as done) */
    if (bios_run_program_pending()) {
        k->cpu.sp = k->sp_init;
        k->cpu.halted = 0;
        mem_clear_fault();
        (void)mem_select_tape(0u);                 /* RUN resets the tape selection */
        mem_clear_fault();
        k->halt_reason = (uint8_t)TOS_HALT_NONE;
        k->resume_state = KS_RUNNING;
        if (k_transit(k, 3)) {                     /* SYSCALL -> RUNNING "program loaded" */
            *bp = 1;
        }
        return -1;
    }
    if (k_transit(k, k->resume_state == KS_RUNNING ? 3 : 2)) {
        *bp = 1;
    }
    return -1;
}

/* ---- public ------------------------------------------------------------- */

void kernel_config_default(kernel_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->tapes = 1u;
    cfg->tape_len = TOS_TAPE_LEN_64K;
    cfg->hz = 0u;
    cfg->seed = 1u;
    cfg->input_mode = (uint8_t)TOS_INPUT_CONSOLE;
    cfg->disks = 1u;
    cfg->trace = 0u;
    cfg->snap_interval = 1000u;
}

void kernel_init(kernel_t *k, const kernel_config_t *cfg)
{
    kernel_config_t c;
    uint32_t L;

    if (k == NULL) {
        return;
    }
    if (cfg != NULL) {
        c = *cfg;
    } else {
        kernel_config_default(&c);
    }
    if (c.tapes != 1u && c.tapes != 2u && c.tapes != 4u) {
        c.tapes = 1u;
    }
    if (c.tape_len != TOS_TAPE_LEN_32K && c.tape_len != TOS_TAPE_LEN_48K && c.tape_len != TOS_TAPE_LEN_64K) {
        c.tape_len = TOS_TAPE_LEN_64K;
    }
    if (c.disks != 1u && c.disks != 2u) {
        c.disks = 1u;
    }
    if (c.input_mode > 1u) {
        c.input_mode = (uint8_t)TOS_INPUT_CONSOLE;
    }
    c.trace = c.trace ? 1u : 0u;
    L = c.tape_len;

    memset(k, 0, sizeof(*k));
    k->cfg = c;
    k->sp_init = (uint16_t)TOS_STACK_TOP(L);
    k->bp_hit = -1;
    k->last_stop = (uint8_t)KSTOP_BUDGET;
    k->halt_reason = (uint8_t)TOS_HALT_NONE;
    k->steps = 0u;
    k->tick = 0u;
    k->frame = 0u;
    k->keys = 0u;
    k->last_syscall = 0u;
    k->last_snapshot_step = 0u;
    g_hal_keys = 0u;
    g_port_keys = 0u;
    g_dirty_since = 1u;

    /* The embedder calls hal_init; the kernel only initialises the machine. */
    mem_init(c.tapes, L);
    trace_reset();
    trace_enable(c.trace ? 1 : 0);
    snapshot_reset();
    bios_init();
    bios_set_tape_len(L);
    bios_set_seed(c.seed);
    (void)fs_init(c.disks);

    cpu_init(&k->cpu);
    k_load_shell();
    k_enter_shell_cpu(k);
    k_set_step(1u);
    k_refresh_ports(k);

    k->state = KS_BOOT;
    k->resume_state = KS_SHELL;
    (void)k_transit(k, 0);          /* BOOT -> SHELL "shell loaded" */
    k->bp_hit = -1;

    kernel_write_meta(k);
    (void)snapshot_save(k);         /* step-0 anchor so any early step can be sought */
}

void kernel_reset(kernel_t *k)
{
    kernel_config_t c;
    if (k == NULL) {
        return;
    }
    c = k->cfg;
    kernel_init(k, &c);
}

kernel_stop_t kernel_step(kernel_t *k, uint32_t max_steps, uint32_t *steps_run)
{
    uint32_t n = 0u;
    kernel_stop_t stop = KSTOP_BUDGET;
    uint32_t L;
    int bp = 0;

    if (k == NULL) {
        if (steps_run != NULL) {
            *steps_run = 0u;
        }
        return KSTOP_HALT;
    }
    L = mem_tape_len();
    g_dirty_since = (uint32_t)k->steps + 1u;
    g_hal_keys = hal_keys();
    k->bp_hit = -1;
    k_set_step((uint32_t)k->steps + 1u);

    for (;;) {
        int r;
        int st;

        if (k->state == KS_HALT) {
            stop = KSTOP_HALT;
            break;
        }

        if (k->state == KS_BOOT) {
            /* Only reachable if someone stepped a never-initialised kernel: boot it now. */
            k_load_shell();
            k_enter_shell_cpu(k);
            k->resume_state = KS_SHELL;
            if (k_transit(k, 0)) {
                stop = KSTOP_BREAKPOINT;
                break;
            }
            continue;
        }

        if (k->state == KS_IDLE) {
            if (!hal_con_in_ready()) {
                stop = KSTOP_WAIT_INPUT;
                break;
            }
            /* Input (or EOF) is available: re-dispatch the parked syscall. EOF while idle is
             * transition 8; a byte is transition 7 followed by the normal SYSCALL handling. */
            r = k_dispatch(k, &bp);
            if (r == BIOS_EOF) {
                k->cpu.io_out_pending = 0u;
                k->halt_reason = (uint8_t)TOS_HALT_EOF;
                (void)k_transit(k, 8);      /* IDLE -> HALT "console EOF" */
                k_drain_output();
                stop = KSTOP_HALT;
                break;
            }
            if (k_transit(k, 7)) {          /* IDLE -> SYSCALL "input available" */
                bp = 1;
            }
            st = k_after_dispatch(k, r, &bp);
            if (k_bp_syscall_access(k) >= 0) {
                bp = 1;
            }
            k_drain_output();
            if (st >= 0) {
                stop = bp ? KSTOP_BREAKPOINT : (kernel_stop_t)st;
                break;
            }
            if (bp) {
                stop = KSTOP_BREAKPOINT;
                break;
            }
            continue;
        }

        if (k->state == KS_SYSCALL) {
            r = k_dispatch(k, &bp);
            if (r == BIOS_EOF) {
                k->cpu.io_out_pending = 0u;
                k->halt_reason = (uint8_t)TOS_HALT_EOF;
                (void)k_transit(k, 11);     /* SYSCALL -> HALT "console EOF" */
                k_drain_output();
                stop = KSTOP_HALT;
                break;
            }
            st = k_after_dispatch(k, r, &bp);
            if (k_bp_syscall_access(k) >= 0) {
                bp = 1;
            }
            k_drain_output();
            if (st >= 0) {
                stop = bp ? KSTOP_BREAKPOINT : (kernel_stop_t)st;
                break;
            }
            if (bp) {
                stop = KSTOP_BREAKPOINT;
                break;
            }
            continue;
        }

        /* KS_SHELL or KS_RUNNING: execute exactly one instruction. */
        {
            uint32_t stamp;
            uint16_t pc;
            int halted_now = 0;
            int hit;

            if (n >= max_steps) {
                stop = KSTOP_BUDGET;
                break;
            }
            pc = k->cpu.pc;
            hit = k_bp_pc(k, pc);
            if (hit >= 0) {
                k->bp_hit = hit;
                stop = KSTOP_BREAKPOINT;
                break;
            }

            k_refresh_ports(k);
            stamp = (uint32_t)k->steps + 1u;
            k_set_step(stamp);
            if (trace_enabled()) {
                uint8_t op = ((uint32_t)pc < L) ? mem_peek(k_tape_for(pc), pc) : 0xFFu;
                trace_push(stamp, pc, (uint8_t)TR_FETCH, op);
            }

            cpu_step(&k->cpu);
            k->steps++;
            n++;

            if (k->cpu.io_out_pending) {
                if (k->cpu.io_out_port == (uint8_t)TOS_PORT_BIOS) {
                    if (mem_fault() != 0u) {
                        if (k_halt_from_exec(k, mem_fault())) {
                            bp = 1;
                        }
                        halted_now = 1;
                    } else {
                        k->resume_state = k->state;
                        if (k_transit(k, k->state == KS_SHELL ? 1 : 4)) {   /* -> SYSCALL "OUT 01" */
                            bp = 1;
                        }
                    }
                } else if (k->cpu.io_out_port == (uint8_t)TOS_PORT_TAPE) {
                    k->cpu.io_out_pending = 0u;
                    (void)mem_select_tape(k->cpu.io_out_value);
                    if (mem_fault() == 0u && trace_enabled()) {
                        trace_push(stamp, (uint16_t)mem_selected_tape(), (uint8_t)TR_TAPE, mem_selected_tape());
                    }
                } else {
                    k->cpu.io_out_pending = 0u;   /* unknown port: ignored */
                }
            }

            if (!halted_now && k->state != KS_SYSCALL) {
                if (mem_fault() != 0u) {
                    /* Tape fault or bad tape select: halt after finishing this instruction. */
                    if (k_halt_from_exec(k, mem_fault())) {
                        bp = 1;
                    }
                    halted_now = 1;
                } else if (k->cpu.halted) {
                    if (k->state == KS_SHELL) {
                        k->halt_reason = (uint8_t)TOS_HALT_COMMAND;
                        if (k_transit(k, 9)) {          /* SHELL -> HALT "halt command" */
                            bp = 1;
                        }
                        halted_now = 1;
                    } else {
                        /* A program's HLT returns to the shell: reload it and restart the CPU. */
                        k_load_shell();
                        k_enter_shell_cpu(k);
                        k->resume_state = KS_SHELL;
                        if (k_transit(k, 5)) {          /* RUNNING -> SHELL "program HLT" */
                            bp = 1;
                        }
                    }
                }
            }

            hit = k_bp_access(k, stamp);
            if (hit >= 0) {
                k->bp_hit = hit;
                bp = 1;
            }
            k_drain_output();

            if (halted_now) {
                stop = KSTOP_HALT;
                break;
            }
            if (bp) {
                stop = KSTOP_BREAKPOINT;
                break;
            }
        }
    }

    k->last_stop = (uint8_t)stop;
    k->tick++;
    kernel_write_meta(k);
    if (k->cfg.snap_interval != 0u &&
        (uint32_t)k->steps - k->last_snapshot_step >= k->cfg.snap_interval) {
        k->last_snapshot_step = (uint32_t)k->steps;
        (void)snapshot_save(k);
        hal_snapshot(mem_raw(), L, mem_raw() + TOS_META_BASE(L), TOS_META_SIZE);
    }
    if (steps_run != NULL) {
        *steps_run = n;
    }
    return stop;
}

void kernel_run(kernel_t *k)
{
    uint32_t t0;
    uint64_t c0;

    if (k == NULL) {
        return;
    }
    t0 = hal_time_ms();
    c0 = k->cpu.cycles;
    for (;;) {
        uint32_t n = 0u;
        uint32_t budget = 4096u;
        kernel_stop_t s;

        if (k->cfg.hz != 0u) {
            budget = k->cfg.hz / 420u + 1u;   /* about one 60 Hz frame of ~7-cycle instructions */
            if (budget > 65536u) {
                budget = 65536u;
            }
        }
        s = kernel_step(k, budget, &n);
        if (s == KSTOP_HALT) {
            return;
        }
        if (s == KSTOP_WAIT_INPUT) {
            /* The posix HAL blocks inside hal_con_in_ready() for pipes and files, so this is only
             * reached on a TTY with nothing typed yet: nap one frame and poll again. */
            hal_vsync();
            continue;
        }
        if (k->cfg.hz != 0u) {
            uint64_t want_ms = (k->cpu.cycles - c0) * 1000u / (uint64_t)k->cfg.hz;
            while ((uint64_t)(uint32_t)(hal_time_ms() - t0) < want_ms) {
                hal_vsync();
            }
        }
    }
}

kernel_state_t kernel_state(const kernel_t *k)
{
    if (k == NULL) {
        return KS_HALT;
    }
    return k->state;
}

void kernel_set_keys(kernel_t *k, uint8_t mask)
{
    if (k == NULL) {
        return;
    }
    k->keys = mask;
    k_refresh_ports(k);
}

void kernel_load_com(kernel_t *k, const uint8_t *bytes, uint32_t len)
{
    uint32_t i;
    if (k == NULL || bytes == NULL) {
        return;
    }
    if (len > TOS_TPA_SIZE) {
        len = TOS_TPA_SIZE;
    }
    for (i = 0u; i < len; i++) {
        mem_poke(0u, (addr_t)(TOS_TPA_BASE + i), bytes[i]);
    }
    /* A host-side load has no table entry: it forces RUNNING without counting a transition. */
    k_enter_shell_cpu(k);
    k->halt_reason = (uint8_t)TOS_HALT_NONE;
    k->state = KS_RUNNING;
    k->resume_state = KS_RUNNING;
    k->bp_hit = -1;
    k_refresh_ports(k);
    kernel_write_meta(k);
}

const kernel_transition_t *kernel_transitions(int *count)
{
    if (count != NULL) {
        *count = KERNEL_TRANSITION_COUNT;
    }
    return k_table;
}

int kernel_bp_add(kernel_t *k, kernel_bp_kind_t kind, uint16_t lo, uint16_t hi)
{
    uint32_t i;
    if (k == NULL) {
        return -1;
    }
    if ((int)kind < (int)KBP_PC || (int)kind > (int)KBP_STATE) {
        return -1;
    }
    if (lo > hi) {
        uint16_t t = lo;
        lo = hi;
        hi = t;
    }
    for (i = 0u; i < KERNEL_BP_MAX; i++) {
        kernel_bp_t *b = &k->bps[i];
        if ((b->active & 1u) == 0u) {
            b->kind = (uint8_t)kind;
            b->active = 1u;
            b->lo = lo;
            b->hi = hi;
            return (int)i;
        }
    }
    return -1;
}

void kernel_bp_clear(kernel_t *k)
{
    if (k == NULL) {
        return;
    }
    memset(k->bps, 0, sizeof(k->bps));
    k->bp_hit = -1;
}

static void k_meta_u16(addr_t base, uint32_t off, uint16_t v)
{
    mem_poke(0u, (addr_t)(base + off), (uint8_t)(v & 0xFFu));
    mem_poke(0u, (addr_t)(base + off + 1u), (uint8_t)((v >> 8) & 0xFFu));
}

static void k_meta_u32(addr_t base, uint32_t off, uint32_t v)
{
    mem_poke(0u, (addr_t)(base + off), (uint8_t)(v & 0xFFu));
    mem_poke(0u, (addr_t)(base + off + 1u), (uint8_t)((v >> 8) & 0xFFu));
    mem_poke(0u, (addr_t)(base + off + 2u), (uint8_t)((v >> 16) & 0xFFu));
    mem_poke(0u, (addr_t)(base + off + 3u), (uint8_t)((v >> 24) & 0xFFu));
}

void kernel_write_meta(kernel_t *k)
{
    uint32_t L;
    addr_t base;
    uint8_t dirty[TOS_META_DIRTY_BYTES];
    uint8_t reason;
    uint32_t i;

    if (k == NULL) {
        return;
    }
    L = mem_tape_len();
    base = (addr_t)TOS_META_BASE(L);

    /* TOS_HALT_BREAKPOINT is informational: shown while the last stop was a breakpoint. */
    reason = k->halt_reason;
    if (k->last_stop == (uint8_t)KSTOP_BREAKPOINT && k->state != KS_HALT) {
        reason = (uint8_t)TOS_HALT_BREAKPOINT;
    }

    mem_poke(0u, (addr_t)(base + TOS_META_STATE), (uint8_t)k->state);
    k_meta_u32(base, TOS_META_STEPS, (uint32_t)k->steps);
    mem_poke(0u, (addr_t)(base + TOS_META_HALT_REASON), reason);
    mem_poke(0u, (addr_t)(base + TOS_META_TAPE_SEL), mem_selected_tape());
    mem_poke(0u, (addr_t)(base + TOS_META_TAPE_COUNT), mem_tape_count());
    k_meta_u16(base, TOS_META_TAPE_PAGES, (uint16_t)(L / 256u));
    k_meta_u32(base, TOS_META_FRAME, k->frame);
    mem_poke(0u, (addr_t)(base + TOS_META_KEYS), (uint8_t)(g_hal_keys | k->keys));
    mem_poke(0u, (addr_t)(base + TOS_META_STOP), k->last_stop);

    memset(dirty, 0, sizeof(dirty));
    mem_dirty_pages(dirty, g_dirty_since);
    for (i = 0u; i < TOS_META_DIRTY_BYTES; i++) {
        mem_poke(0u, (addr_t)(base + TOS_META_DIRTY + i), dirty[i]);
    }

    mem_poke(0u, (addr_t)(base + TOS_META_SEED), k->cfg.seed);
    k_meta_u32(base, TOS_META_HZ, k->cfg.hz);
    mem_poke(0u, (addr_t)(base + TOS_META_INPUT_MODE), k->cfg.input_mode);
    mem_poke(0u, (addr_t)(base + TOS_META_DISKS), k->cfg.disks);
    mem_poke(0u, (addr_t)(base + TOS_META_TRACE), k->cfg.trace);
    mem_poke(0u, (addr_t)(base + TOS_META_SYSCALL), k->last_syscall);
    mem_poke(0u, (addr_t)(base + TOS_META_SYSCALL + 1u), 0u);
    k_meta_u16(base, TOS_META_SP_INIT, k->sp_init);
}
