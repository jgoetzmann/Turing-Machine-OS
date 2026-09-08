/* TuringOS v2 — BIOS ("ROM services").
 *
 * The 8080 reaches the host only through OUT 0x01 (function id in A) and this file.
 * Every host interaction goes through the HAL (src/hal/hal.h) or the in-memory
 * filesystem / compilers; no <stdio.h> here.
 *
 * Parking protocol: a service that needs console input and finds none returns
 * BIOS_WAIT without touching any register and without clearing cpu->io_out_pending,
 * so the kernel can park in KS_IDLE and call bios_dispatch again with the same cpu.
 *
 * All BIOS state lives in one static struct so snapshots can memcpy it (bios_state_*). */
#include "bios.h"

#include "../tos.h"
#include "../emu/mem.h"
#include "../fs/fs.h"
#include "../hal/hal.h"
#include "../compiler/compiler.h"
#include "../lang/asm.h"
#include "../lang/tm.h"
#include "../lang/bf.h"

#include <stdint.h>
#include <string.h>

#define BIOS_OUT_CAP   4096u
#define BIOS_NAME_CAP  64u
#define BIOS_LINE_CAP  128u
#define BIOS_SRC_CAP   32768u
#define BIOS_COM_CAP   16384u
#define BIOS_SECTOR    256u
#define BIOS_ERR_CAP   256u

typedef struct {
    uint8_t  out[BIOS_OUT_CAP];       /* console output ring */
    uint32_t out_head;
    uint32_t out_tail;
    uint32_t out_count;
    uint8_t  disk;                    /* SELDISK */
    uint8_t  track;                   /* SETTRK */
    uint8_t  sector;                  /* SETSEC (1-based) */
    uint16_t dma;                     /* SETDMA */
    uint32_t tape_len;                /* L, for DMA bounds */
    char     name[BIOS_NAME_CAP];     /* NAMECH accumulator */
    uint32_t name_len;
    uint8_t  run_pending;             /* 1 once after a successful RUN */
    char     line[BIOS_LINE_CAP];     /* READLINE buffer */
    uint32_t line_len;
    uint8_t  line_active;             /* a READLINE is in progress (parked) */
    uint8_t  seed;
    uint8_t  rng;                     /* xorshift state */
    uint32_t ticks;                   /* frames completed */
    uint8_t  last_fn;
} bios_state_t;

static bios_state_t S;

/* Transient scratch for the compilers and RUN — not part of the snapshot state. */
static uint8_t g_src[BIOS_SRC_CAP + 1u];
static uint8_t g_com[BIOS_COM_CAP];
static char    g_err[BIOS_ERR_CAP];

/* ---- output ring ------------------------------------------------------- */

static void out_byte(uint8_t ch) {
    if (S.out_count >= BIOS_OUT_CAP) {
        /* Ring full (a long TYPE/LISTDIR inside one syscall): hand the oldest byte to the
         * host now so nothing is lost and ordering holds; the kernel drains the rest later. */
        hal_con_out(S.out[S.out_tail]);
        S.out_tail = (S.out_tail + 1u) % BIOS_OUT_CAP;
        S.out_count--;
    }
    S.out[S.out_head] = ch;
    S.out_head = (S.out_head + 1u) % BIOS_OUT_CAP;
    S.out_count++;
}

static void out_str(const char *s) {
    while (*s != '\0') {
        out_byte((uint8_t)*s);
        s++;
    }
}

static void out_bad(void) {
    out_str("?\n");
}

/* ---- name buffer helpers ----------------------------------------------- */

/* Copy the accumulated name out (NUL-terminated) and clear the accumulator. */
static void name_take(char *dst, uint32_t cap) {
    uint32_t n = S.name_len;
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, S.name, n);
    dst[n] = '\0';
    S.name_len = 0u;
    S.name[0] = '\0';
}

/* Split "BASE.EXT" -> base; returns 1 when an extension was present. */
static int name_base(const char *name, char *base, uint32_t cap) {
    uint32_t i = 0u;
    int has_ext = 0;
    while (name[i] != '\0' && name[i] != '.' && i + 1u < cap) {
        base[i] = name[i];
        i++;
    }
    base[i] = '\0';
    if (name[i] == '.') {
        has_ext = 1;
    }
    return has_ext;
}

static void name_join(char *dst, uint32_t cap, const char *base, const char *ext) {
    uint32_t n = 0u;
    uint32_t i;
    for (i = 0u; base[i] != '\0' && n + 1u < cap; i++) {
        dst[n++] = base[i];
    }
    if (n + 1u < cap) {
        dst[n++] = '.';
    }
    for (i = 0u; ext[i] != '\0' && n + 1u < cap; i++) {
        dst[n++] = ext[i];
    }
    dst[n] = '\0';
}

/* ---- individual services ----------------------------------------------- */

static int svc_conin(cpu_t *cpu) {
    int ch = hal_con_in();
    if (ch == -1) {
        return BIOS_WAIT;               /* registers and io_out_pending untouched */
    }
    if (ch == -2) {
        cpu->a = 0u;
        return BIOS_EOF;
    }
    cpu->a = (uint8_t)ch;
    return BIOS_DONE;
}

static int svc_readline(void) {
    if (!S.line_active) {
        S.line_active = 1u;
        S.line_len = 0u;
        S.line[0] = '\0';
    }
    for (;;) {
        int ch = hal_con_in();
        if (ch == -1) {
            return BIOS_WAIT;           /* partial line stays in S.line; resume on the next dispatch */
        }
        if (ch == -2) {
            if (S.line_len > 0u) {
                /* EOF right after a partial line: deliver the line now, EOF on the next call. */
                S.line_active = 0u;
                break;
            }
            S.line_active = 0u;
            return BIOS_EOF;
        }
        if (ch == '\n' || ch == '\r') {
            S.line_active = 0u;
            break;
        }
        if (ch == 8 || ch == 127) {
            if (S.line_len > 0u) {
                S.line_len--;
            }
            continue;
        }
        if (S.line_len < BIOS_LINE_CAP) {
            S.line[S.line_len++] = (char)ch;
        }
    }
    if (S.line_len < BIOS_LINE_CAP) {
        S.line[S.line_len] = '\0';
    }
    return BIOS_DONE;
}

static void svc_lineget(cpu_t *cpu) {
    uint32_t idx = cpu->c;
    if (idx < S.line_len && idx < BIOS_LINE_CAP) {
        cpu->a = (uint8_t)S.line[idx];
    } else {
        cpu->a = 0u;
    }
}

static void svc_linelen(cpu_t *cpu) {
    cpu->a = (S.line_len > 255u) ? 255u : (uint8_t)S.line_len;
}

static void svc_seldisk(cpu_t *cpu) {
    if (fs_select_disk(cpu->c) == 0) {
        S.disk = cpu->c;
        cpu->a = 0u;
    } else {
        cpu->a = 1u;
    }
}

static void svc_read(cpu_t *cpu) {
    uint8_t buf[BIOS_SECTOR];
    uint32_t i;
    if ((uint32_t)S.dma + BIOS_SECTOR > S.tape_len) {
        cpu->a = 1u;
        return;
    }
    if (fs_read_sector(S.track, S.sector, buf) != 0) {
        cpu->a = 1u;
        return;
    }
    for (i = 0u; i < BIOS_SECTOR; i++) {
        mem_write((addr_t)((uint32_t)S.dma + i), buf[i]);
    }
    cpu->a = 0u;
}

static void svc_write(cpu_t *cpu) {
    uint8_t buf[BIOS_SECTOR];
    uint32_t i;
    if ((uint32_t)S.dma + BIOS_SECTOR > S.tape_len) {
        cpu->a = 1u;
        return;
    }
    for (i = 0u; i < BIOS_SECTOR; i++) {
        buf[i] = mem_read((addr_t)((uint32_t)S.dma + i));
    }
    if (fs_write_sector(S.track, S.sector, buf) != 0) {
        cpu->a = 1u;
        return;
    }
    fs_flush();     /* write-through, like the compile path: the image on the host stays current */
    cpu->a = 0u;
}

static void svc_listdir(void) {
    char names[TOS_DISK_DIR_ENTRIES][13];
    int n;
    int i;
    int j;
    n = fs_list(names, (int)TOS_DISK_DIR_ENTRIES);
    if (n < 0) {
        out_bad();
        return;
    }
    if (n == 0) {
        out_str("(empty)\n");
        return;
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < 12 && names[i][j] != '\0'; j++) {
            out_byte((uint8_t)names[i][j]);
        }
        out_byte((uint8_t)'\n');
    }
}

static void svc_namech(cpu_t *cpu) {
    if (S.name_len + 1u < BIOS_NAME_CAP) {
        S.name[S.name_len++] = (char)cpu->c;
        S.name[S.name_len] = '\0';
    }
}

static void svc_type(void) {
    char name[BIOS_NAME_CAP];
    uint8_t buf[BIOS_SECTOR];
    int fh;
    int r;
    int i;
    name_take(name, sizeof name);
    if (name[0] == '\0') {
        out_bad();
        return;
    }
    fh = fs_open(name);
    if (fh < 0) {
        out_bad();
        return;
    }
    for (;;) {
        r = fs_read(fh, buf, (int)sizeof buf);
        if (r <= 0) {
            break;
        }
        for (i = 0; i < r; i++) {
            out_byte(buf[i]);
        }
    }
    fs_close(fh);
    out_byte((uint8_t)'\n');
}

static void svc_run(cpu_t *cpu) {
    char raw[BIOS_NAME_CAP];
    char base[BIOS_NAME_CAP];
    char name[BIOS_NAME_CAP + 8u];
    int size;
    int n;
    int i;
    name_take(raw, sizeof raw);
    if (raw[0] == '\0') {
        out_bad();
        return;
    }
    if (name_base(raw, base, sizeof base)) {
        memcpy(name, raw, sizeof raw);
    } else {
        name_join(name, sizeof name, base, "COM");
    }
    size = fs_file_size(name);
    if (size < 0 || (uint32_t)size > TOS_TPA_SIZE) {
        out_bad();
        return;
    }
    n = fs_get_file(name, g_com, BIOS_COM_CAP);
    if (n < 0 || (uint32_t)n > TOS_TPA_SIZE) {
        out_bad();
        return;
    }
    (void)mem_select_tape(0u);          /* RUN resets the tape selection */
    for (i = 0; i < n; i++) {
        mem_poke(0u, (addr_t)(TOS_TPA_BASE + (uint32_t)i), g_com[i]);
    }
    cpu->pc = (uint16_t)TOS_TPA_BASE;
    cpu->halted = 0;
    S.run_pending = 1u;
}

static void svc_del(void) {
    char name[BIOS_NAME_CAP];
    name_take(name, sizeof name);
    if (name[0] == '\0') {
        out_bad();
        return;
    }
    if (fs_delete(name) != 0) {
        out_bad();
        return;
    }
    fs_flush();     /* a delete the host never sees is a delete that did not happen */
}

/* lang: TOS_LANG_C / TOS_LANG_ASM / TOS_LANG_TM / TOS_LANG_BF */
static void svc_compile(int lang) {
    char raw[BIOS_NAME_CAP];
    char base[BIOS_NAME_CAP];
    char src_name[BIOS_NAME_CAP + 8u];
    char com_name[BIOS_NAME_CAP + 8u];
    const char *ext;
    int size;
    int n;
    int len;

    name_take(raw, sizeof raw);
    if (raw[0] == '\0') {
        out_bad();
        return;
    }
    if (lang == TOS_LANG_ASM) {
        ext = "ASM";
    } else if (lang == TOS_LANG_TM) {
        ext = "TM";
    } else if (lang == TOS_LANG_BF) {
        ext = "BF";
    } else {
        ext = "C";
    }
    if (name_base(raw, base, sizeof base)) {
        memcpy(src_name, raw, sizeof raw);
    } else {
        name_join(src_name, sizeof src_name, base, ext);
    }
    name_join(com_name, sizeof com_name, base, "COM");

    size = fs_file_size(src_name);
    if (size < 0 || (uint32_t)size > BIOS_SRC_CAP) {
        out_bad();
        return;
    }
    n = fs_get_file(src_name, g_src, BIOS_SRC_CAP);
    if (n < 0 || (uint32_t)n > BIOS_SRC_CAP) {
        out_bad();
        return;
    }
    g_src[n] = 0u;
    g_err[0] = '\0';

    if (lang == TOS_LANG_ASM) {
        len = asm_assemble((const char *)g_src, (uint32_t)n, g_com, TOS_TPA_SIZE, g_err, BIOS_ERR_CAP);
    } else if (lang == TOS_LANG_TM) {
        len = tm_compile((const char *)g_src, (uint32_t)n, g_com, TOS_TPA_SIZE, g_err, BIOS_ERR_CAP);
    } else if (lang == TOS_LANG_BF) {
        len = bf_compile((const char *)g_src, (uint32_t)n, g_com, TOS_TPA_SIZE, g_err, BIOS_ERR_CAP);
    } else {
        len = cc_compile_buf((const char *)g_src, (uint32_t)n, g_com, TOS_TPA_SIZE, g_err, BIOS_ERR_CAP);
    }
    if (len < 0) {
        g_err[BIOS_ERR_CAP - 1u] = '\0';
        if (g_err[0] == '\0') {
            out_str("?");
        } else if (memcmp(g_err, "src.c:", 6u) == 0) {
            out_str(src_name);          /* "src.c:L:C: msg" -> "PONG.C:L:C: msg" */
            out_str(g_err + 5);
        } else {
            out_str(g_err);
        }
        out_byte((uint8_t)'\n');
        return;
    }
    if (fs_put_file(com_name, g_com, (uint32_t)len) != 0) {
        out_bad();
        return;
    }
    fs_flush();
}

/* ---- public API -------------------------------------------------------- */

void bios_init(void) {
    memset(&S, 0, sizeof S);
    S.sector = 1u;
    S.tape_len = TOS_TAPE_LEN_64K;
    S.dma = (uint16_t)TOS_DMA_DEFAULT(TOS_TAPE_LEN_64K);
    S.seed = 1u;
    S.rng = 1u;
}

void bios_reset(void) {
    uint32_t tape_len = S.tape_len;
    uint8_t seed = S.seed;
    bios_init();
    bios_set_tape_len(tape_len);
    bios_set_seed(seed);
}

void bios_set_tape_len(uint32_t tape_len) {
    if (tape_len != TOS_TAPE_LEN_32K && tape_len != TOS_TAPE_LEN_48K && tape_len != TOS_TAPE_LEN_64K) {
        tape_len = TOS_TAPE_LEN_64K;
    }
    S.tape_len = tape_len;
    S.dma = (uint16_t)TOS_DMA_DEFAULT(tape_len);
}

void bios_set_seed(uint8_t seed) {
    if (seed == 0u) {
        seed = 1u;                      /* seed 0 behaves as seed 1 */
    }
    S.seed = seed;
    S.rng = seed;
}

int bios_dispatch(cpu_t *cpu) {
    int r = BIOS_DONE;
    uint8_t fn;

    if (cpu == NULL) {
        return BIOS_DONE;
    }
    fn = cpu->io_out_value;
    S.last_fn = fn;

    switch (fn) {
        case TOS_BIOS_CONIN:
            r = svc_conin(cpu);
            if (r == BIOS_WAIT) {
                return BIOS_WAIT;       /* io_out_pending stays set for the retry */
            }
            break;
        case TOS_BIOS_CONOUT:
            out_byte(cpu->c);
            break;
        case TOS_BIOS_AUXOUT:
            break;
        case TOS_BIOS_AUXIN:
            cpu->a = 0u;
            break;
        case TOS_BIOS_CONST:
            cpu->a = hal_con_in_ready() ? 0xFFu : 0u;
            break;
        case TOS_BIOS_VSYNC:
            r = BIOS_VSYNC;
            break;
        case TOS_BIOS_RAND:
            cpu->a = bios_rand();
            break;
        case TOS_BIOS_TICKS:
            cpu->a = (uint8_t)(S.ticks & 0xFFu);
            break;
        case TOS_BIOS_SELDISK:
            svc_seldisk(cpu);
            break;
        case TOS_BIOS_SETTRK:
            S.track = cpu->c;
            break;
        case TOS_BIOS_SETSEC:
            S.sector = cpu->c;
            break;
        case TOS_BIOS_SETDMA:
            S.dma = (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e);
            break;
        case TOS_BIOS_READ:
            svc_read(cpu);
            break;
        case TOS_BIOS_WRITE:
            svc_write(cpu);
            break;
        case TOS_BIOS_LISTDIR:
            svc_listdir();
            break;
        case TOS_BIOS_NAMECH:
            svc_namech(cpu);
            break;
        case TOS_BIOS_TYPE:
            svc_type();
            break;
        case TOS_BIOS_RUN:
            svc_run(cpu);
            break;
        case TOS_BIOS_DEL:
            svc_del();
            break;
        case TOS_BIOS_CC:
            svc_compile(TOS_LANG_C);
            break;
        case TOS_BIOS_READLINE:
            r = svc_readline();
            if (r == BIOS_WAIT) {
                return BIOS_WAIT;
            }
            break;
        case TOS_BIOS_LINEGET:
            svc_lineget(cpu);
            break;
        case TOS_BIOS_LINELEN:
            svc_linelen(cpu);
            break;
        case TOS_BIOS_ASM:
            svc_compile(TOS_LANG_ASM);
            break;
        case TOS_BIOS_TM:
            svc_compile(TOS_LANG_TM);
            break;
        case TOS_BIOS_BF:
            svc_compile(TOS_LANG_BF);
            break;
        default:
            break;                      /* unknown function id: ignored */
    }

    cpu->io_out_pending = 0u;
    return r;
}

int bios_run_program_pending(void) {
    int r = S.run_pending ? 1 : 0;
    S.run_pending = 0u;
    return r;
}

int bios_pending_output(void) {
    return (int)S.out_count;
}

char bios_get_output(void) {
    char ch = '\0';
    if (S.out_count > 0u) {
        ch = (char)S.out[S.out_tail];
        S.out_tail = (S.out_tail + 1u) % BIOS_OUT_CAP;
        S.out_count--;
    }
    return ch;
}

uint8_t bios_current_disk(void) {
    return S.disk;
}

uint8_t bios_current_track(void) {
    return S.track;
}

uint8_t bios_current_sector(void) {
    return S.sector;
}

uint16_t bios_dma_addr(void) {
    return S.dma;
}

uint8_t bios_rand(void) {
    uint8_t x = S.rng;
    if (x == 0u) {
        x = 1u;
    }
    x ^= (uint8_t)(x << 3);
    x ^= (uint8_t)(x >> 5);
    x ^= (uint8_t)(x << 1);
    S.rng = x;
    return x;
}

uint32_t bios_ticks(void) {
    return S.ticks;
}

void bios_tick(void) {
    S.ticks++;
}

uint8_t bios_last_fn(void) {
    return S.last_fn;
}

uint32_t bios_state_size(void) {
    return (uint32_t)sizeof S;
}

void bios_state_save(uint8_t *buf) {
    if (buf != NULL) {
        memcpy(buf, &S, sizeof S);
    }
}

void bios_state_load(const uint8_t *buf) {
    if (buf != NULL) {
        memcpy(&S, buf, sizeof S);
        if (S.out_head >= BIOS_OUT_CAP || S.out_tail >= BIOS_OUT_CAP || S.out_count > BIOS_OUT_CAP) {
            S.out_head = 0u;
            S.out_tail = 0u;
            S.out_count = 0u;
        }
        if (S.name_len >= BIOS_NAME_CAP) {
            S.name_len = 0u;
        }
        if (S.line_len > BIOS_LINE_CAP) {
            S.line_len = 0u;
        }
        if (S.tape_len != TOS_TAPE_LEN_32K && S.tape_len != TOS_TAPE_LEN_48K && S.tape_len != TOS_TAPE_LEN_64K) {
            S.tape_len = TOS_TAPE_LEN_64K;
        }
    }
}
