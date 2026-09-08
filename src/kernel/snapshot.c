/* TuringOS v2 — ring of full machine snapshots.
 * Each slot holds every tape (k x 65536), the cpu (inside kernel_t), the whole kernel_t, the BIOS
 * state (bios_state_*) and the filesystem state (fs_state_*). Never disk images, ages or trace.
 * Static slots only: no heap. */
#include "snapshot.h"

#include "../bios/bios.h"
#include "../emu/mem.h"
#include "../fs/fs.h"
#include "../tos.h"

#include <string.h>

#define SNAP_BIOS_MAX 16384u
#define SNAP_FS_MAX   32768u

typedef struct {
    uint8_t  used;
    uint8_t  tapes;        /* number of tapes copied */
    uint8_t  sel;          /* selected tape at save time (mem state, not in kernel_t) */
    uint8_t  fault;        /* pending mem fault at save time */
    uint32_t seq;          /* monotonic save counter; the newest slot has the highest seq */
    uint32_t step;         /* low 32 bits of k->steps at save time */
    uint32_t tape_len;
    uint32_t bios_len;     /* 0 when the BIOS state did not fit */
    uint32_t fs_len;       /* 0 when the FS state did not fit */
    uint8_t  tape[TOS_TAPES_MAX][TOS_TAPE_MAX];
    kernel_t kernel;
    uint8_t  bios[SNAP_BIOS_MAX];
    uint8_t  fs[SNAP_FS_MAX];
} snap_slot_t;

static snap_slot_t g_slots[SNAPSHOT_SLOTS];
static uint32_t    g_seq;
static int         g_count;

void snapshot_reset(void)
{
    uint32_t i;

    for (i = 0u; i < SNAPSHOT_SLOTS; i++) {
        g_slots[i].used = 0u;
        g_slots[i].tapes = 0u;
        g_slots[i].sel = 0u;
        g_slots[i].fault = 0u;
        g_slots[i].seq = 0u;
        g_slots[i].step = 0u;
        g_slots[i].tape_len = 0u;
        g_slots[i].bios_len = 0u;
        g_slots[i].fs_len = 0u;
    }
    g_seq = 0u;
    g_count = 0;
}

int snapshot_save(const kernel_t *k)
{
    uint32_t idx;
    snap_slot_t *s;
    uint8_t tapes;
    uint32_t n;
    uint8_t t;

    if (k == NULL) {
        return -1;
    }
    /* Slot 0 keeps the anchor a seek to an early step needs; the rest of the ring rotates and
       overwrites its own oldest slot. A later snapshot taken at the anchor's own step replaces it,
       because that is the state that step has on the current timeline: loading a program stores an
       anchor at step 0 too, and resurrecting the pre-load machine would be a different run. */
    if (g_seq == 0u || (g_slots[0].used && g_slots[0].step == (uint32_t)k->steps)) {
        idx = 0u;
    } else {
        idx = 1u + ((g_seq - 1u) % (SNAPSHOT_SLOTS - 1u));
    }
    s = &g_slots[idx];
    tapes = mem_tape_count();
    if (tapes > TOS_TAPES_MAX) {
        tapes = (uint8_t)TOS_TAPES_MAX;
    }

    g_seq++;
    s->used = 1u;
    s->seq = g_seq;
    s->step = (uint32_t)k->steps;
    s->tapes = tapes;
    s->sel = mem_selected_tape();
    s->fault = mem_fault();
    s->tape_len = mem_tape_len();
    for (t = 0u; t < tapes; t++) {
        memcpy(s->tape[t], mem_tape_raw(t), TOS_TAPE_MAX);
    }
    memcpy(&s->kernel, k, sizeof(kernel_t));

    n = bios_state_size();
    if (n != 0u && n <= SNAP_BIOS_MAX) {
        bios_state_save(s->bios);
        s->bios_len = n;
    } else {
        s->bios_len = 0u;
    }
    n = fs_state_size();
    if (n != 0u && n <= SNAP_FS_MAX) {
        fs_state_save(s->fs);
        s->fs_len = n;
    } else {
        s->fs_len = 0u;
    }
    if (g_count < (int)SNAPSHOT_SLOTS) {
        g_count++;
    }
    return (int)idx;
}

int snapshot_restore(kernel_t *k, int slot)
{
    const snap_slot_t *s;
    kernel_config_t cfg;
    kernel_bp_t bps[KERNEL_BP_MAX];
    uint8_t t;

    if (k == NULL || slot < 0 || slot >= (int)SNAPSHOT_SLOTS) {
        return -1;
    }
    s = &g_slots[slot];
    if (!s->used) {
        return -1;
    }
    if (s->tape_len != mem_tape_len() || s->tapes != mem_tape_count()) {
        return -1;                     /* taken on a differently shaped machine */
    }
    for (t = 0u; t < s->tapes; t++) {
        memcpy(mem_tape_raw(t), s->tape[t], TOS_TAPE_MAX);
    }

    /* View-level state (levers, breakpoints) belongs to the host and survives a restore. */
    cfg = k->cfg;
    memcpy(bps, k->bps, sizeof(bps));
    memcpy(k, &s->kernel, sizeof(kernel_t));
    k->cfg = cfg;
    memcpy(k->bps, bps, sizeof(bps));
    k->bp_hit = -1;

    /* The ages are not in the snapshot (they would cost 256 KB per tape per slot), so anything
       stamped after the step just restored describes a future that no longer happened. Clearing
       those stamps is an approximation: a cell written both before and after the target reads as
       never written afterwards. It is the smaller lie, and mem_forget_after keeps the
       cells-written counter consistent with what the arrays now say. */
    {
        uint8_t tt;
        for (tt = 0u; tt < s->tapes; tt++) {
            mem_forget_after(tt, s->step);
        }
    }

    mem_clear_fault();
    if (s->sel < mem_tape_count()) {
        (void)mem_select_tape(s->sel);
    } else {
        (void)mem_select_tape(0u);
    }
    mem_clear_fault();
    mem_set_step((uint32_t)k->steps + 1u);

    if (s->bios_len != 0u && s->bios_len == bios_state_size()) {
        bios_state_load(s->bios);
    }
    if (s->fs_len != 0u && s->fs_len == fs_state_size()) {
        fs_state_load(s->fs);
    }
    return 0;
}

int snapshot_find(uint32_t step)
{
    int best = -1;
    uint32_t i;

    for (i = 0u; i < SNAPSHOT_SLOTS; i++) {
        const snap_slot_t *s = &g_slots[i];
        if (!s->used || s->step > step) {
            continue;
        }
        if (best < 0 ||
            s->step > g_slots[best].step ||
            (s->step == g_slots[best].step && s->seq > g_slots[best].seq)) {
            best = (int)i;
        }
    }
    return best;
}

int snapshot_count(void)
{
    return g_count;
}

uint32_t snapshot_step(int slot)
{
    if (slot < 0 || slot >= (int)SNAPSHOT_SLOTS || !g_slots[slot].used) {
        return 0u;
    }
    return g_slots[slot].step;
}
