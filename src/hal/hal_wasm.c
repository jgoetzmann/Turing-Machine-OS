/* TuringOS v2 — WebAssembly (Emscripten) host abstraction layer.
 *
 * The browser/Node host talks to the machine only through the tos_* API, which in turn
 * uses these queues: console bytes arrive via hal_con_push (4096-byte ring) and leave via
 * hal_con_out into a 64 KB ring that the API layer drains with hal_con_out_pop().
 * There is no stdin, no file and no clock that can influence machine state:
 * input never reaches EOF (the page decides when input stops), keys come from
 * hal_keys_set, disks are filled by JS through fs_image_ptr + tos_disk_reload. */
#include "hal.h"
#include "../tos.h"

#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#define HAL_IN_CAP  4096u
#define HAL_OUT_CAP 65536u

static uint8_t  g_in[HAL_IN_CAP];
static uint32_t g_in_head = 0u, g_in_tail = 0u, g_in_count = 0u;

static uint8_t  g_out[HAL_OUT_CAP];
static uint32_t g_out_head = 0u, g_out_tail = 0u, g_out_count = 0u;

static uint8_t  g_keys = 0u;

void hal_init(void) {
    g_in_head = g_in_tail = g_in_count = 0u;
    g_out_head = g_out_tail = g_out_count = 0u;
    g_keys = 0u;
}

void hal_shutdown(void) {
    g_in_head = g_in_tail = g_in_count = 0u;
    g_out_head = g_out_tail = g_out_count = 0u;
    g_keys = 0u;
}

int hal_set_option(const char *key, const char *value) {
    (void)key;
    (void)value;
    return -1;                      /* no host options in the browser */
}

/* ---- console ----------------------------------------------------------- */

void hal_con_out(uint8_t ch) {
    if (g_out_count >= HAL_OUT_CAP) {
        g_out_tail = (g_out_tail + 1u) % HAL_OUT_CAP;   /* drop the oldest */
        g_out_count--;
    }
    g_out[g_out_head] = ch;
    g_out_head = (g_out_head + 1u) % HAL_OUT_CAP;
    g_out_count++;
}

/* Extra helpers (not in hal.h): the API layer's tos_con_pop/tos_con_pending read these. */
int hal_con_out_pending(void) {
    return (int)g_out_count;
}

int hal_con_out_pop(void) {
    int ch;
    if (g_out_count == 0u) {
        return -1;
    }
    ch = g_out[g_out_tail];
    g_out_tail = (g_out_tail + 1u) % HAL_OUT_CAP;
    g_out_count--;
    return ch;
}

int hal_con_in_ready(void) {
    return (g_in_count > 0u) ? 1 : 0;
}

int hal_con_in(void) {
    int ch;
    if (g_in_count == 0u) {
        return -1;                  /* never EOF: the page decides when input stops */
    }
    ch = g_in[g_in_tail];
    g_in_tail = (g_in_tail + 1u) % HAL_IN_CAP;
    g_in_count--;
    return ch;
}

int hal_con_push(uint8_t ch) {
    if (g_in_count >= HAL_IN_CAP) {
        return -1;
    }
    g_in[g_in_head] = ch;
    g_in_head = (g_in_head + 1u) % HAL_IN_CAP;
    g_in_count++;
    return 0;
}

/* ---- keys -------------------------------------------------------------- */

uint8_t hal_keys(void) {
    return g_keys;
}

void hal_keys_set(uint8_t mask) {
    g_keys = mask;
}

/* ---- time / frames ----------------------------------------------------- */

uint32_t hal_time_ms(void) {
#ifdef __EMSCRIPTEN__
    return (uint32_t)emscripten_get_now();
#else
    return 0u;
#endif
}

void hal_vsync(void) {
    /* The page paces frames with requestAnimationFrame; nothing to do here. */
}

/* ---- disks ------------------------------------------------------------- */

int hal_disk_load(uint8_t disk, uint8_t *buf, uint32_t cap) {
    (void)disk;
    (void)buf;
    (void)cap;
    return 0;                       /* JS writes into tos_disk_ptr() then calls tos_disk_reload() */
}

int hal_disk_save(uint8_t disk, const uint8_t *buf, uint32_t len) {
    (void)disk;
    (void)buf;
    (void)len;
    return 0;                       /* the image buffer is the persistent copy; JS reads it back */
}

/* ---- shell blob -------------------------------------------------------- */

int hal_shell_blob(const uint8_t **ptr, uint32_t *len) {
    if (ptr == NULL || len == NULL || tos_shell_blob_len == 0u) {
        return -1;
    }
    *ptr = tos_shell_blob;
    *len = tos_shell_blob_len;
    return 0;
}

/* ---- observation hooks -------------------------------------------------- */

void hal_snapshot(const uint8_t *tape, uint32_t tape_len, const uint8_t *meta, uint32_t meta_len) {
    (void)tape;
    (void)tape_len;
    (void)meta;
    (void)meta_len;                 /* the page reads snapshots through tos_snapshot_* */
}

void hal_display(const uint8_t *fb) {
    (void)fb;                       /* the page reads tos_tape_ptr(0) + TOS_DISPLAY_BASE itself */
}
