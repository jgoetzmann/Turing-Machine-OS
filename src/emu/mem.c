/* src/emu/mem.c — TuringOS v2 tapes.
 *
 * k tapes (1|2|4) of L bytes (32K|48K|64K). Addresses inside the banked
 * window [TOS_BANK_BASE, TOS_BANK_END(L)] resolve to the selected tape;
 * every other address resolves to tape 0. Addresses >= L fault.
 *
 * All state is static. No heap, no <stdio.h>.
 */
#include "mem.h"
#include "../tos.h"
#include "../kernel/trace.h"

#include <string.h>

#define MEM_TAPES_MAX  4u
#define MEM_TAPE_BYTES 65536u

/* ---- backing store ---------------------------------------------------- */
static uint8_t  g_tape[MEM_TAPES_MAX][MEM_TAPE_BYTES];
static uint32_t g_wage[MEM_TAPES_MAX][MEM_TAPE_BYTES];   /* step of last write, 0 = never */
static uint32_t g_rage[MEM_TAPES_MAX][MEM_TAPE_BYTES];   /* step of last read,  0 = never */

/* ---- geometry / selection / fault ------------------------------------- */
static uint8_t  g_tapes    = 1u;
static uint32_t g_len      = TOS_TAPE_LEN_64K;
static uint32_t g_bank_end = TOS_BANK_END(TOS_TAPE_LEN_64K);
static uint8_t  g_sel      = 0u;
static uint8_t  g_fault    = TOS_HALT_NONE;

/* ---- observation ------------------------------------------------------ */
static uint32_t g_step          = 0u;
static uint64_t g_travel        = 0u;
static uint64_t g_accesses      = 0u;
static uint32_t g_cells_written = 0u;
static uint16_t g_last_addr     = 0u;
static uint8_t  g_have_last     = 0u;

/* Which tape backs `addr` right now. */
static uint8_t mem_route(addr_t addr) {
    const uint32_t a = (uint32_t)addr;
    if (a >= TOS_BANK_BASE && a <= g_bank_end) {
        return g_sel;
    }
    return 0u;
}

/* Head movement bookkeeping: travel is the sum of |addr_i - addr_(i-1)|,
 * so the very first access contributes 0 (WS4-08: 0x0100,0x0110,0x0105 -> 27). */
static void mem_head_move(addr_t addr) {
    if (g_have_last) {
        const uint16_t d = (addr > g_last_addr) ? (uint16_t)(addr - g_last_addr)
                                                : (uint16_t)(g_last_addr - addr);
        g_travel += (uint64_t)d;
    } else {
        g_have_last = 1u;
    }
    g_last_addr = addr;
    g_accesses++;
}

static void mem_raise_fault(uint8_t reason) {
    if (g_fault == TOS_HALT_NONE) {
        g_fault = reason;
    }
}

/* ---- lifecycle -------------------------------------------------------- */

void mem_init(uint8_t tapes, uint32_t tape_len) {
    if (tapes >= 4u) {
        tapes = 4u;
    } else if (tapes >= 2u) {
        tapes = 2u;
    } else {
        tapes = 1u;
    }
    if (tape_len != TOS_TAPE_LEN_32K && tape_len != TOS_TAPE_LEN_48K) {
        tape_len = TOS_TAPE_LEN_64K;
    }

    memset(g_tape, 0, sizeof(g_tape));
    memset(g_wage, 0, sizeof(g_wage));
    memset(g_rage, 0, sizeof(g_rage));

    g_tapes    = tapes;
    g_len      = tape_len;
    g_bank_end = TOS_BANK_END(tape_len);
    g_sel      = 0u;
    g_fault    = TOS_HALT_NONE;

    g_step          = 0u;
    g_travel        = 0u;
    g_accesses      = 0u;
    g_cells_written = 0u;
    g_last_addr     = 0u;
    g_have_last     = 0u;
}

/* ---- machine-visible access ------------------------------------------- */

uint8_t mem_read(addr_t addr) {
    uint8_t t;
    uint8_t v;
    if ((uint32_t)addr >= g_len) {
        mem_raise_fault(TOS_HALT_TAPE_FAULT);
        return 0xFFu;
    }
    t = mem_route(addr);
    v = g_tape[t][addr];
    g_rage[t][addr] = g_step;
    mem_head_move(addr);
    if (trace_enabled()) {
        trace_push(g_step, addr, TR_READ, v);
    }
    return v;
}

void mem_write(addr_t addr, uint8_t val) {
    uint8_t t;
    if ((uint32_t)addr >= g_len) {
        mem_raise_fault(TOS_HALT_TAPE_FAULT);
        return;
    }
    t = mem_route(addr);
    g_tape[t][addr] = val;
    if (g_wage[t][addr] == 0u && g_step != 0u) {
        g_cells_written++;
    }
    g_wage[t][addr] = g_step;
    mem_head_move(addr);
    if (trace_enabled()) {
        trace_push(g_step, addr, TR_WRITE, val);
    }
}

/* ---- raw access (no side effects, no fault) --------------------------- */

uint8_t mem_peek(uint8_t tape, addr_t addr) {
    return g_tape[tape & 3u][addr];
}

void mem_poke(uint8_t tape, addr_t addr, uint8_t val) {
    g_tape[tape & 3u][addr] = val;
}

uint8_t *mem_raw(void) {
    return g_tape[0];
}

uint8_t *mem_tape_raw(uint8_t tape) {
    return g_tape[tape & 3u];
}

/* ---- geometry / selection --------------------------------------------- */

uint8_t mem_tape_count(void) {
    return g_tapes;
}

uint32_t mem_tape_len(void) {
    return g_len;
}

int mem_select_tape(uint8_t n) {
    if (n >= g_tapes) {
        mem_raise_fault(TOS_HALT_BAD_TAPE);
        return -1;
    }
    g_sel = n;
    return 0;
}

uint8_t mem_selected_tape(void) {
    return g_sel;
}

uint8_t mem_fault(void) {
    return g_fault;
}

void mem_clear_fault(void) {
    g_fault = TOS_HALT_NONE;
}

/* ---- observation ------------------------------------------------------ */

void mem_set_step(uint32_t step) {
    g_step = step;
}

uint32_t *mem_write_age(uint8_t tape) {
    return g_wage[tape & 3u];
}

uint32_t *mem_read_age(uint8_t tape) {
    return g_rage[tape & 3u];
}

/* Bit p of `out` (out[p >> 3], bit p & 7, LSB first) is set when any byte of
 * 256-byte page p on any tape carries a write age >= since_step (and != 0).
 * Tape 0 is scanned over the whole tape length; tapes 1..k-1 only over the
 * banked window, the only place a write can land on them. Once a page is
 * found dirty the scan skips to the next page. */
void mem_dirty_pages(uint8_t out[32], uint32_t since_step) {
    uint8_t t;
    memset(out, 0, 32u);
    for (t = 0u; t < g_tapes; t++) {
        const uint32_t *age = g_wage[t];
        const uint32_t lo = (t == 0u) ? 0u : TOS_BANK_BASE;
        const uint32_t hi = (t == 0u) ? g_len : (g_bank_end + 1u);
        uint32_t a;
        for (a = lo; a < hi; a++) {
            const uint32_t w = age[a];
            if (w != 0u && w >= since_step) {
                const uint32_t page = a >> 8u;
                out[page >> 3u] |= (uint8_t)(1u << (page & 7u));
                a |= 0xFFu;   /* jump to the last byte of this page */
            }
        }
    }
}

uint64_t mem_travel(void) {
    return g_travel;
}

uint64_t mem_accesses(void) {
    return g_accesses;
}

/* After a snapshot restore, ages stamped later than the restored step describe a timeline that no
 * longer exists. Drop them and recount, so mem_cells_written keeps matching the arrays. */
void mem_forget_after(uint8_t tape, uint32_t step) {
    uint32_t a;
    uint8_t t;
    if (tape >= MEM_TAPES_MAX) {
        return;
    }
    for (a = 0u; a < MEM_TAPE_BYTES; a++) {
        if (g_wage[tape][a] > step) g_wage[tape][a] = 0u;
        if (g_rage[tape][a] > step) g_rage[tape][a] = 0u;
    }
    g_cells_written = 0u;
    for (t = 0u; t < MEM_TAPES_MAX; t++) {
        for (a = 0u; a < MEM_TAPE_BYTES; a++) {
            if (g_wage[t][a] != 0u) g_cells_written++;
        }
    }
}

uint32_t mem_cells_written(void) {
    return g_cells_written;
}
