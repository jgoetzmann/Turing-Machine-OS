/* TuringOS v2 — the single embedding API. One global machine, one input log, one output ring.
 * Used by main.c, the C tests and (exported) the WebAssembly build. No stdio, no heap.
 *
 * Time travel: every tos_con_push / tos_keys_set / tos_load_com is appended to a 4096-entry input
 * log tagged with the step it happened at and the kernel tick (number of kernel_step calls so
 * far). tos_seek restores the newest snapshot at or before the target and re-runs the kernel one
 * instruction at a time, re-applying logged inputs at the steps they were recorded at. Entries
 * that lie beyond the target stay pending and are re-applied by later tos_step calls, so
 * "seek then step" reproduces the recorded timeline; a new push discards the pending future. */
#include "api.h"

#include "../bios/bios.h"
#include "../compiler/compiler.h"
#include "../emu/cpu.h"
#include "../emu/disasm.h"
#include "../emu/mem.h"
#include "../fs/fs.h"
#include "../hal/hal.h"
#include "../kernel/kernel.h"
#include "../kernel/snapshot.h"
#include "../kernel/trace.h"
#include "../lang/asm.h"
#include "../lang/bf.h"
#include "../lang/tm.h"
#include "../tos.h"

#include <stddef.h>
#include <string.h>

/* Kernel-side hook (defined in kernel.c, not part of kernel.h): every byte the kernel drains
 * out of the BIOS output ring goes through this function instead of hal_con_out. */
void kernel_set_con_out(void (*fn)(uint8_t ch));

/* HAL helper outside hal.h (both implementations provide it): bytes we pushed that the machine
 * has not consumed yet. */
uint32_t hal_con_push_pending(void);

/* ---- state -------------------------------------------------------------- */

#define API_LOG_CAP    4096u
#define API_OUT_CAP    65536u
#define API_LOAD_SLOTS 8u
#define API_KIND_CON   0u
#define API_KIND_KEYS  1u
#define API_KIND_LOAD  2u

typedef struct {
    uint32_t step;      /* k->steps when the input was (last) applied */
    uint32_t tick;      /* k->tick when the input was (last) applied */
    uint32_t tag;       /* API_KIND_LOAD: which load filled the slot (g_load_next at the time) */
    uint8_t  kind;      /* API_KIND_* */
    uint8_t  value;     /* byte / key mask / load slot */
    uint8_t  pad0;
    uint8_t  pad1;
} api_input_t;

static kernel_t     g_k;
static int          g_created;

static api_input_t  g_log[API_LOG_CAP];
static uint32_t     g_log_len;
static uint32_t     g_replay_pos;             /* first log entry not yet (re)applied; == g_log_len when none pending */
static uint32_t     g_pushed_total;           /* console bytes pushed into the HAL since create/reset */
static int          g_replaying;              /* 1 while tos_seek re-runs the machine (host output suppressed) */
static uint32_t     g_replay_seen_upto;       /* during a replay, the last step whose output the host already saw */
static int          g_seek_restoring;         /* 1 while a failed seek is putting the machine back */

static uint8_t      g_out[API_OUT_CAP];       /* console output ring */
static uint8_t      g_pending[API_OUT_CAP];   /* output a replay produced that is not final yet */
static uint32_t     g_pending_len;
static uint32_t     g_out_head;               /* total bytes written */
static uint32_t     g_out_tail;               /* total bytes read */

static uint8_t      g_load_img[API_LOAD_SLOTS][TOS_TPA_SIZE];
static uint32_t     g_load_len[API_LOAD_SLOTS];
static uint32_t     g_load_serial[API_LOAD_SLOTS];   /* which load each slot currently holds */
static uint32_t     g_load_next;
static int          g_replay_broken;                 /* a replayed load referred to a recycled slot */

static const char *const k_state_names[6] = { "BOOT", "IDLE", "SHELL", "RUNNING", "SYSCALL", "HALT" };

/* ---- helpers ------------------------------------------------------------ */

static void api_emit(uint8_t ch)
{
    hal_con_out(ch);
    if (g_out_head - g_out_tail >= API_OUT_CAP) {
        g_out_tail++;               /* full: drop the oldest byte */
    }
    g_out[g_out_head % API_OUT_CAP] = ch;
    g_out_head++;
}

static void api_con_out(uint8_t ch)
{
    if (g_replaying) {
        if ((uint32_t)g_k.steps <= g_replay_seen_upto) {
            return;                 /* the host already saw this output the first time round */
        }
        /* Past that point the replay is producing output for the first time, but the seek can
         * still fail and throw those steps away. Hold it until we know. */
        if (g_pending_len < API_OUT_CAP) {
            g_pending[g_pending_len++] = ch;
        }
        return;
    }
    api_emit(ch);
}

/* Pull back out of the HAL queue the bytes we pushed that the machine has not consumed yet
 * (bounded by what we pushed, so scripted stdin is never touched). */
static void api_drain_pushed(void)
{
    uint32_t queued = hal_con_push_pending();
    uint32_t i;
    /* Only what is still sitting in the push queue is ours to take back. Draining `g_pushed_total`
     * blindly would eat bytes the machine already consumed, i.e. the host's stdin script. */
    if (queued > g_pushed_total) {
        queued = g_pushed_total;
    }
    for (i = 0u; i < queued; i++) {
        if (hal_con_in() < 0) {
            break;
        }
    }
    g_pushed_total = 0u;
}

static void api_clear_host_state(void)
{
    api_drain_pushed();
    g_log_len = 0u;
    g_replay_pos = 0u;
    g_pushed_total = 0u;
    g_out_head = 0u;
    g_out_tail = 0u;
    g_pending_len = 0u;
    g_replaying = 0;
    memset(g_load_len, 0, sizeof(g_load_len));
    memset(g_load_serial, 0, sizeof(g_load_serial));
    g_load_next = 0u;
    g_replay_broken = 0;
}

static int api_config_valid(const kernel_config_t *c)
{
    if (c->tapes != 1u && c->tapes != 2u && c->tapes != 4u) {
        return 0;
    }
    if (c->tape_len != TOS_TAPE_LEN_32K && c->tape_len != TOS_TAPE_LEN_48K && c->tape_len != TOS_TAPE_LEN_64K) {
        return 0;
    }
    if (c->disks != 1u && c->disks != 2u) {
        return 0;
    }
    if (c->input_mode > 1u) {
        return 0;
    }
    if (c->trace > 1u) {
        return 0;
    }
    return 1;
}

static int api_recreate(const kernel_config_t *c)
{
    if (!api_config_valid(c)) {
        return -1;
    }
    kernel_set_con_out(api_con_out);
    api_clear_host_state();
    kernel_init(&g_k, c);
    hal_keys_set(0u);
    g_created = 1;
    return 0;
}

static void api_ensure(void)
{
    if (!g_created) {
        kernel_config_t c;
        kernel_config_default(&c);
        (void)api_recreate(&c);
    }
}

/* Apply one input to the machine right now and stamp the entry with where it was applied. */
static void api_apply(api_input_t *e)
{
    e->step = (uint32_t)g_k.steps;
    e->tick = g_k.tick;
    if (e->kind == (uint8_t)API_KIND_CON) {
        (void)hal_con_push(e->value);
        g_pushed_total++;
    } else if (e->kind == (uint8_t)API_KIND_KEYS) {
        kernel_set_keys(&g_k, e->value);
        hal_keys_set(e->value);
    } else if (e->kind == (uint8_t)API_KIND_LOAD) {
        uint32_t slot = e->value % API_LOAD_SLOTS;
        if (g_load_len[slot] == 0u || g_load_serial[slot] != e->tag) {
            /* Only the last API_LOAD_SLOTS images are kept. This entry names one that has been
             * overwritten, so replaying it would load a different program: refuse instead. */
            g_replay_broken = 1;
        } else {
            kernel_load_com(&g_k, g_load_img[slot], g_load_len[slot]);
        }
    }
}

/* Record a fresh host input: a pending future (from an earlier seek) is discarded first, since the
 * timeline has just branched. Applies it immediately. */
static void api_record_tagged(uint8_t kind, uint8_t value, uint32_t tag)
{
    api_input_t *e;
    api_input_t tmp;

    if (g_replay_pos < g_log_len) {
        g_log_len = g_replay_pos;
    }
    if (g_log_len >= API_LOG_CAP) {
        /* Log full: the input still reaches the machine, it just cannot be replayed. */
        tmp.kind = kind;
        tmp.value = value;
        tmp.tag = tag;
        tmp.pad0 = 0u;
        tmp.pad1 = 0u;
        api_apply(&tmp);
        return;
    }
    e = &g_log[g_log_len];
    e->kind = kind;
    e->value = value;
    e->tag = tag;
    e->pad0 = 0u;
    e->pad1 = 0u;
    api_apply(e);
    g_log_len++;
    g_replay_pos = g_log_len;
}

static void api_record(uint8_t kind, uint8_t value)
{
    api_record_tagged(kind, value, 0u);
}

/* Re-apply every pending entry recorded at or before the current step. */
static void api_apply_due(void)
{
    while (g_replay_pos < g_log_len && g_log[g_replay_pos].step <= (uint32_t)g_k.steps) {
        api_apply(&g_log[g_replay_pos]);
        g_replay_pos++;
    }
}

static uint8_t api_tape_for(uint16_t addr)
{
    uint32_t L = mem_tape_len();
    if ((uint32_t)addr >= (uint32_t)TOS_BANK_BASE && (uint32_t)addr <= TOS_BANK_END(L)) {
        return mem_selected_tape();
    }
    return 0u;
}

static void api_set_err(char *err, uint32_t errcap, const char *msg)
{
    uint32_t i = 0u;
    if (err == NULL || errcap == 0u) {
        return;
    }
    while (msg[i] != '\0' && i + 1u < errcap) {
        err[i] = msg[i];
        i++;
    }
    err[i] = '\0';
}

/* ---- machine lifecycle -------------------------------------------------- */

TOS_EXPORT int tos_create(const tos_config_t *cfg)
{
    kernel_config_t c;
    if (cfg != NULL) {
        c = *cfg;
    } else {
        kernel_config_default(&c);
    }
    return api_recreate(&c);
}

TOS_EXPORT void tos_reset(void)
{
    kernel_config_t c;
    api_ensure();
    c = g_k.cfg;
    (void)api_recreate(&c);
}

TOS_EXPORT uint32_t tos_step(uint32_t max_steps)
{
    uint32_t total = 0u;

    api_ensure();
    for (;;) {
        uint32_t n = 0u;
        uint32_t chunk;
        kernel_stop_t s;

        /* Pending inputs from a seek are fed back exactly at the steps they were recorded at. */
        api_apply_due();
        chunk = max_steps - total;
        if (g_replay_pos < g_log_len) {
            uint32_t gap = g_log[g_replay_pos].step - (uint32_t)g_k.steps;
            if (gap < chunk) {
                chunk = gap;
            }
        }
        s = kernel_step(&g_k, chunk, &n);
        total += n;
        if (s != KSTOP_BUDGET || total >= max_steps || g_replay_pos >= g_log_len) {
            break;
        }
    }
    return total;
}

TOS_EXPORT int tos_stop_reason(void)
{
    return (int)g_k.last_stop;
}

TOS_EXPORT int tos_state(void)
{
    return (int)g_k.state;
}

TOS_EXPORT uint32_t tos_steps(void)
{
    return (uint32_t)g_k.steps;
}

TOS_EXPORT uint32_t tos_cycles_lo(void)
{
    return (uint32_t)(g_k.cpu.cycles & 0xFFFFFFFFu);
}

TOS_EXPORT uint32_t tos_cycles_hi(void)
{
    return (uint32_t)(g_k.cpu.cycles >> 32);
}

TOS_EXPORT uint8_t tos_halt_reason(void)
{
    return g_k.halt_reason;
}

TOS_EXPORT uint32_t tos_frame(void)
{
    return g_k.frame;
}

TOS_EXPORT uint8_t tos_last_syscall(void)
{
    return g_k.last_syscall;
}

/* ---- tape access -------------------------------------------------------- */

TOS_EXPORT uint8_t *tos_tape_ptr(uint8_t tape)
{
    api_ensure();
    if (tape >= mem_tape_count()) {
        tape = 0u;
    }
    return mem_tape_raw(tape);
}

TOS_EXPORT uint8_t tos_tape_count(void)
{
    api_ensure();
    return mem_tape_count();
}

TOS_EXPORT uint32_t tos_tape_len(void)
{
    api_ensure();
    return mem_tape_len();
}

TOS_EXPORT uint8_t tos_tape_selected(void)
{
    api_ensure();
    return mem_selected_tape();
}

TOS_EXPORT uint8_t *tos_cpu_ptr(void)
{
    api_ensure();
    return (uint8_t *)&g_k.cpu;
}

TOS_EXPORT uint8_t *tos_meta_ptr(void)
{
    api_ensure();
    return mem_raw() + TOS_META_BASE(mem_tape_len());
}

TOS_EXPORT uint32_t *tos_write_age_ptr(uint8_t tape)
{
    api_ensure();
    if (tape >= mem_tape_count()) {
        tape = 0u;
    }
    return mem_write_age(tape);
}

TOS_EXPORT uint32_t *tos_read_age_ptr(uint8_t tape)
{
    api_ensure();
    if (tape >= mem_tape_count()) {
        tape = 0u;
    }
    return mem_read_age(tape);
}

TOS_EXPORT uint32_t tos_travel_lo(void)
{
    return (uint32_t)(mem_travel() & 0xFFFFFFFFu);
}

TOS_EXPORT uint32_t tos_travel_hi(void)
{
    return (uint32_t)(mem_travel() >> 32);
}

TOS_EXPORT uint32_t tos_accesses_lo(void)
{
    return (uint32_t)(mem_accesses() & 0xFFFFFFFFu);
}

TOS_EXPORT uint32_t tos_cells_written(void)
{
    return mem_cells_written();
}

/* ---- trace -------------------------------------------------------------- */

TOS_EXPORT const trace_event_t *tos_trace_ptr(void)
{
    return trace_ring();
}

TOS_EXPORT uint32_t tos_trace_head(void)
{
    return trace_head();
}

TOS_EXPORT uint32_t tos_trace_count(void)
{
    return trace_count();
}

TOS_EXPORT void tos_trace_enable(int on)
{
    api_ensure();
    trace_enable(on ? 1 : 0);
    g_k.cfg.trace = on ? 1u : 0u;
    kernel_write_meta(&g_k);
}

/* ---- console & keys ----------------------------------------------------- */

TOS_EXPORT void tos_con_push(uint8_t ch)
{
    api_ensure();
    api_record((uint8_t)API_KIND_CON, ch);
}

TOS_EXPORT int tos_con_pop(void)
{
    uint8_t ch;
    if (g_out_head == g_out_tail) {
        return -1;
    }
    ch = g_out[g_out_tail % API_OUT_CAP];
    g_out_tail++;
    return (int)ch;
}

TOS_EXPORT int tos_con_pending(void)
{
    return (int)(g_out_head - g_out_tail);
}

TOS_EXPORT void tos_keys_set(uint8_t mask)
{
    api_ensure();
    api_record((uint8_t)API_KIND_KEYS, mask);
}

/* ---- disks -------------------------------------------------------------- */

TOS_EXPORT uint8_t *tos_disk_ptr(uint8_t disk)
{
    api_ensure();
    if (disk >= fs_disk_count()) {
        disk = 0u;
    }
    return fs_image_ptr(disk);
}

TOS_EXPORT uint32_t tos_disk_size(void)
{
    return fs_image_size();
}

TOS_EXPORT void tos_disk_reload(uint8_t disk)
{
    api_ensure();
    if (disk >= fs_disk_count()) {
        return;
    }
    fs_reload(disk);
}

TOS_EXPORT int tos_disk_put_file(uint8_t disk, const char *name, const uint8_t *data, uint32_t len)
{
    uint8_t prev;
    int r;
    api_ensure();
    if (name == NULL || disk >= fs_disk_count()) {
        return -1;
    }
    prev = fs_selected_disk();
    if (fs_select_disk(disk) != 0) {
        return -1;
    }
    r = fs_put_file(name, data, len);
    (void)fs_select_disk(prev);
    return r == 0 ? 0 : -1;
}

TOS_EXPORT int tos_disk_get_file(uint8_t disk, const char *name, uint8_t *out, uint32_t cap)
{
    uint8_t prev;
    int r;
    api_ensure();
    if (name == NULL || disk >= fs_disk_count()) {
        return -1;
    }
    prev = fs_selected_disk();
    if (fs_select_disk(disk) != 0) {
        return -1;
    }
    r = fs_get_file(name, out, cap);
    (void)fs_select_disk(prev);
    return r;
}

TOS_EXPORT int tos_disk_list(uint8_t disk, char *out, uint32_t cap)
{
    char names[TOS_DISK_DIR_ENTRIES][13];
    uint8_t prev;
    int n;
    int i;
    uint32_t pos = 0u;

    api_ensure();
    if (out != NULL && cap != 0u) {
        out[0] = '\0';
    }
    if (disk >= fs_disk_count()) {
        return -1;
    }
    prev = fs_selected_disk();
    if (fs_select_disk(disk) != 0) {
        return -1;
    }
    n = fs_list(names, (int)TOS_DISK_DIR_ENTRIES);
    (void)fs_select_disk(prev);
    if (n < 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        const char *s = names[i];
        while (*s != '\0') {
            if (out != NULL && pos + 1u < cap) {
                out[pos] = *s;
            }
            pos++;
            s++;
        }
        if (out != NULL && pos + 1u < cap) {
            out[pos] = '\n';
        }
        pos++;
    }
    if (out != NULL && cap != 0u) {
        out[pos < cap ? pos : cap - 1u] = '\0';
    }
    return n;
}

/* ---- levers, breakpoints, time travel ----------------------------------- */

TOS_EXPORT int tos_lever_set(int id, uint32_t value)
{
    kernel_config_t c;
    api_ensure();
    c = g_k.cfg;
    switch (id) {
    case TOS_LEVER_TAPES:
        if (value != 1u && value != 2u && value != 4u) {
            return -1;
        }
        c.tapes = (uint8_t)value;
        return api_recreate(&c);
    case TOS_LEVER_TAPE_LEN:
        if (value != TOS_TAPE_LEN_32K && value != TOS_TAPE_LEN_48K && value != TOS_TAPE_LEN_64K) {
            return -1;
        }
        c.tape_len = value;
        return api_recreate(&c);
    case TOS_LEVER_HZ:
        g_k.cfg.hz = value;
        kernel_write_meta(&g_k);
        return 0;
    case TOS_LEVER_SEED:
        if (value > 255u) {
            return -1;
        }
        c.seed = (uint8_t)value;
        return api_recreate(&c);
    case TOS_LEVER_INPUT_MODE:
        if (value > 1u) {
            return -1;
        }
        g_k.cfg.input_mode = (uint8_t)value;
        kernel_write_meta(&g_k);
        return 0;
    case TOS_LEVER_DISKS:
        if (value != 1u && value != 2u) {
            return -1;
        }
        c.disks = (uint8_t)value;
        return api_recreate(&c);
    case TOS_LEVER_TRACE:
        if (value > 1u) {
            return -1;
        }
        g_k.cfg.trace = (uint8_t)value;
        trace_enable((int)value);
        kernel_write_meta(&g_k);
        return 0;
    case TOS_LEVER_SNAP_INTERVAL:
        g_k.cfg.snap_interval = value;
        kernel_write_meta(&g_k);
        return 0;
    default:
        return -1;
    }
}

TOS_EXPORT uint32_t tos_lever_get(int id)
{
    api_ensure();
    switch (id) {
    case TOS_LEVER_TAPES:         return (uint32_t)g_k.cfg.tapes;
    case TOS_LEVER_TAPE_LEN:      return g_k.cfg.tape_len;
    case TOS_LEVER_HZ:            return g_k.cfg.hz;
    case TOS_LEVER_SEED:          return (uint32_t)g_k.cfg.seed;
    case TOS_LEVER_INPUT_MODE:    return (uint32_t)g_k.cfg.input_mode;
    case TOS_LEVER_DISKS:         return (uint32_t)g_k.cfg.disks;
    case TOS_LEVER_TRACE:         return (uint32_t)g_k.cfg.trace;
    case TOS_LEVER_SNAP_INTERVAL: return g_k.cfg.snap_interval;
    default:                      return 0u;
    }
}

TOS_EXPORT int tos_bp_add(int kind, uint16_t lo, uint16_t hi)
{
    api_ensure();
    if (kind < (int)KBP_PC || kind > (int)KBP_STATE) {
        return -1;
    }
    return kernel_bp_add(&g_k, (kernel_bp_kind_t)kind, lo, hi);
}

TOS_EXPORT void tos_bp_clear(void)
{
    api_ensure();
    kernel_bp_clear(&g_k);
}

TOS_EXPORT int tos_bp_hit(void)
{
    return g_k.bp_hit;
}

TOS_EXPORT int tos_seek(uint32_t step)
{
    int slot;
    uint32_t snap_step;
    uint32_t snap_tick;
    uint32_t i;
    uint32_t stall = 0u;
    uint32_t from_step;
    int trace_was_on;

    api_ensure();
    slot = snapshot_find(step);
    if (slot < 0) {
        return -1;
    }
    from_step = (uint32_t)g_k.steps;
    snap_step = snapshot_step(slot);

    /* Unconsumed bytes we pushed would be fed twice once the log is replayed: pull them out. */
    api_drain_pushed();

    if (snapshot_restore(&g_k, slot) != 0) {
        return -1;
    }
    snap_tick = g_k.tick;
    hal_keys_set(g_k.keys);

    /* Everything recorded before the snapshot is already baked into it. An entry at the snapshot's
     * own step came after it exactly when its tick is >= the tick the snapshot was taken at. */
    i = 0u;
    while (i < g_log_len) {
        const api_input_t *e = &g_log[i];
        if (e->step > snap_step || (e->step == snap_step && e->tick >= snap_tick)) {
            /* tos_load_com stores its anchor immediately after applying the entry, at the same
             * step and tick, so that load is already inside this snapshot: replaying it would
             * reload the image (and fail if the slot has since been recycled) for nothing.
             * Console bytes at the same tick still have to be re-pushed, because the HAL's input
             * queue is not part of a snapshot. */
            if (e->kind == (uint8_t)API_KIND_LOAD && e->step == snap_step && e->tick == snap_tick) {
                i++;
                continue;
            }
            break;
        }
        i++;
    }
    g_replay_pos = i;

    /* The trace already holds these steps; re-running them must not write them a second time. */
    trace_was_on = trace_enabled();
    if (trace_was_on) {
        trace_enable(0);
    }
    g_replaying = 1;
    g_replay_broken = 0;
    g_pending_len = 0u;
    /* Output is suppressed for steps the host has already seen. When a failed seek is putting the
     * machine back, every step up to the target is such a step. */
    g_replay_seen_upto = g_seek_restoring ? step : from_step;
    while ((uint32_t)g_k.steps < step && g_k.state != KS_HALT) {
        uint32_t before = (uint32_t)g_k.steps;
        uint32_t n = 0u;
        kernel_stop_t s;

        api_apply_due();
        s = kernel_step(&g_k, 1u, &n);
        if ((uint32_t)g_k.steps == before) {
            /* No instruction ran: parked without a logged byte, or a breakpoint edge. Give the
             * machine a few chances (breakpoints clear themselves), then give up. */
            if (s == KSTOP_WAIT_INPUT || s == KSTOP_HALT) {
                break;
            }
            stall++;
            if (stall > 8u) {
                break;
            }
        } else {
            stall = 0u;
        }
    }
    g_replaying = 0;
    if (trace_was_on) {
        trace_enable(1);
    }
    kernel_write_meta(&g_k);
    if ((uint32_t)g_k.steps == step && !g_replay_broken) {
        uint32_t i;
        for (i = 0u; i < g_pending_len; i++) {
            api_emit(g_pending[i]);   /* the steps happened: the output is real */
        }
        g_pending_len = 0u;
        return 0;
    }
    g_pending_len = 0u;               /* the timeline was thrown away; so is its output */
    /* The target could not be reached (the log has no input the machine is waiting for, or it
     * halted first). Put the machine back where the caller had it instead of leaving it stranded
     * at some intermediate step. */
    if (!g_seek_restoring && (uint32_t)g_k.steps != from_step) {
        g_seek_restoring = 1;
        (void)tos_seek(from_step);
        g_seek_restoring = 0;
    }
    return -1;
}

TOS_EXPORT int tos_snapshot_count(void)
{
    return snapshot_count();
}

TOS_EXPORT uint32_t tos_snapshot_step(int slot)
{
    return snapshot_step(slot);
}

/* ---- programs & tools --------------------------------------------------- */

TOS_EXPORT int tos_load_com(const uint8_t *bytes, uint32_t len)
{
    uint32_t slot;
    api_ensure();
    if (bytes == NULL || len == 0u || len > TOS_TPA_SIZE) {
        return -1;
    }
    slot = g_load_next % API_LOAD_SLOTS;
    g_load_serial[slot] = g_load_next;
    g_load_next++;
    memcpy(g_load_img[slot], bytes, len);
    g_load_len[slot] = len;
    api_record_tagged((uint8_t)API_KIND_LOAD, (uint8_t)slot, g_load_serial[slot]);
    /* Anchor the freshly loaded state so a seek right after the load never has to replay the
     * whole shell session that came before it. */
    g_k.last_snapshot_step = (uint32_t)g_k.steps;
    (void)snapshot_save(&g_k);
    return 0;
}

TOS_EXPORT int tos_compile(int lang, const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap)
{
    if (src == NULL || out == NULL) {
        api_set_err(err, errcap, "src.c:1:1: no source");
        return -1;
    }
    switch (lang) {
    case TOS_LANG_C:
        return cc_compile_buf(src, len, out, cap, err, errcap);
    case TOS_LANG_ASM:
        return asm_assemble(src, len, out, cap, err, errcap);
    case TOS_LANG_TM:
        return tm_compile(src, len, out, cap, err, errcap);
    case TOS_LANG_BF:
        return bf_compile(src, len, out, cap, err, errcap);
    default:
        api_set_err(err, errcap, "line 1: unknown language");
        return -1;
    }
}

TOS_EXPORT int tos_disasm(uint16_t addr, char *out, int cap)
{
    uint8_t b[3];
    uint32_t L;
    uint32_t i;

    api_ensure();
    L = mem_tape_len();
    for (i = 0u; i < 3u; i++) {
        uint32_t a = (uint32_t)addr + i;
        if (a < L && a <= 0xFFFFu) {
            b[i] = mem_peek(api_tape_for((uint16_t)a), (addr_t)a);
        } else {
            b[i] = 0xFFu;
        }
    }
    return disasm_one(b, addr, out, cap);
}

TOS_EXPORT const char *tos_state_name(int state)
{
    if (state < 0 || state > 5) {
        return "?";
    }
    return k_state_names[state];
}

TOS_EXPORT const char *tos_syscall_name(int fn)
{
    switch (fn) {
    case TOS_BIOS_CONIN:    return "CONIN";
    case TOS_BIOS_CONOUT:   return "CONOUT";
    case TOS_BIOS_AUXOUT:   return "AUXOUT";
    case TOS_BIOS_AUXIN:    return "AUXIN";
    case TOS_BIOS_CONST:    return "CONST";
    case TOS_BIOS_VSYNC:    return "VSYNC";
    case TOS_BIOS_RAND:     return "RAND";
    case TOS_BIOS_TICKS:    return "TICKS";
    case TOS_BIOS_SELDISK:  return "SELDISK";
    case TOS_BIOS_SETTRK:   return "SETTRK";
    case TOS_BIOS_SETSEC:   return "SETSEC";
    case TOS_BIOS_SETDMA:   return "SETDMA";
    case TOS_BIOS_READ:     return "READ";
    case TOS_BIOS_WRITE:    return "WRITE";
    case TOS_BIOS_LISTDIR:  return "LISTDIR";
    case TOS_BIOS_NAMECH:   return "NAMECH";
    case TOS_BIOS_TYPE:     return "TYPE";
    case TOS_BIOS_RUN:      return "RUN";
    case TOS_BIOS_DEL:      return "DEL";
    case TOS_BIOS_CC:       return "CC";
    case TOS_BIOS_READLINE: return "READLINE";
    case TOS_BIOS_LINEGET:  return "LINEGET";
    case TOS_BIOS_LINELEN:  return "LINELEN";
    case TOS_BIOS_ASM:      return "ASM";
    case TOS_BIOS_TM:       return "TM";
    case TOS_BIOS_BF:       return "BF";
    default:                return "?";
    }
}

TOS_EXPORT int tos_transition_count(void)
{
    int n = 0;
    (void)kernel_transitions(&n);
    return n;
}

TOS_EXPORT const char *tos_transition_why(int index)
{
    int n = 0;
    const kernel_transition_t *t = kernel_transitions(&n);
    if (index < 0 || index >= n) {
        return "?";
    }
    return t[index].why;
}

TOS_EXPORT int tos_transition_from(int index)
{
    int n = 0;
    const kernel_transition_t *t = kernel_transitions(&n);
    if (index < 0 || index >= n) {
        return -1;
    }
    return (int)t[index].from;
}

TOS_EXPORT int tos_transition_to(int index)
{
    int n = 0;
    const kernel_transition_t *t = kernel_transitions(&n);
    if (index < 0 || index >= n) {
        return -1;
    }
    return (int)t[index].to;
}

TOS_EXPORT uint32_t tos_transition_fired(int index)
{
    if (index < 0 || index >= KERNEL_TRANSITION_COUNT) {
        return 0u;
    }
    return g_k.transition_counts[index];
}

TOS_EXPORT int tos_hal_option(const char *key, const char *value)
{
    if (key == NULL) {
        return -1;
    }
    return hal_set_option(key, value == NULL ? "" : value);
}

TOS_EXPORT const char *tos_version(void)
{
    return TOS_VERSION;
}
