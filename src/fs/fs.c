/* TuringOS v2 — CP/M-style flat filesystem over static disk-image buffers.
 *
 * Every disk is a TOS_DISK_IMAGE_BYTES (512512) byte image held in a static
 * buffer. fs_init pulls each image in through hal_disk_load; fs_flush pushes
 * dirty images back out through hal_disk_save. Nothing here touches <stdio.h>.
 *
 * Image layout (77 tracks x 26 sectors x 256 bytes):
 *   bytes 0..2047        directory: 64 entries x 32 bytes (block 0, track 0)
 *   bytes 2048..511999   data blocks 1..249, 2048 bytes (8 sectors) each
 *   bytes 512000..512511 unused tail (a partial block)
 *
 * Directory entry (32 bytes):
 *   [0]     status: 0x00 active, 0xE5 deleted / never used
 *   [1-8]   name, upper-case, space padded
 *   [9-11]  extension, upper-case, space padded
 *   [12]    extent number (always 0: one entry per file)
 *   [13-14] u16 LE exact file length in bytes (the "reserved" bytes)
 *   [15]    record count = ceil(length / 256)
 *   [16-31] allocation map: 16 block numbers (1..249), 0 = slot unused
 *
 * One entry addresses 16 x 2 KB = 32 KB, so files up to 32768 bytes work with
 * a single extent. The directory is parsed straight out of the image buffer
 * (there is no separate cache that could go stale), so fs_reload only has to
 * drop handles whose entries vanished. Snapshot state = open table + selection.
 */
#include "fs.h"
#include "../tos.h"
#include "../hal/hal.h"

#include <stddef.h>
#include <string.h>

#define FS_DIR_ENTRIES     TOS_DISK_DIR_ENTRIES                      /* 64 */
#define FS_DIR_ENTRY_SIZE  TOS_DISK_DIR_ENTRY                        /* 32 */
#define FS_DIR_BYTES       (FS_DIR_ENTRIES * FS_DIR_ENTRY_SIZE)      /* 2048 */
#define FS_BLOCK_BYTES     2048u
#define FS_BLOCK_COUNT     (TOS_DISK_IMAGE_BYTES / FS_BLOCK_BYTES)   /* 250; block 0 = directory */
#define FS_ENTRY_BLOCKS    16u
#define FS_FILE_MAX        (FS_ENTRY_BLOCKS * FS_BLOCK_BYTES)        /* 32768 */
#define FS_OPEN_MAX        16u
#define FS_STATE_BYTES     (4u + FS_OPEN_MAX * 8u)                   /* 132 */

#define FS_E_STATUS  0u
#define FS_E_NAME    1u
#define FS_E_EXT     9u
#define FS_E_EXTENT  12u
#define FS_E_LEN_LO  13u
#define FS_E_LEN_HI  14u
#define FS_E_RECS    15u
#define FS_E_ALLOC   16u

#define FS_ACTIVE    0x00u
#define FS_DELETED   0xE5u

typedef struct {
    uint8_t  in_use;
    uint8_t  disk;
    uint8_t  dir_index;
    uint8_t  pad;
    uint32_t pos;
} fs_handle_t;

static uint8_t     g_img[TOS_DISKS_MAX][TOS_DISK_IMAGE_BYTES];
static uint8_t     g_dirty[TOS_DISKS_MAX];
static uint8_t     g_disk_count = 0u;
static uint8_t     g_selected = 0u;
static fs_handle_t g_open[FS_OPEN_MAX];

/* ---- name helpers ------------------------------------------------------ */

static uint8_t fs_upper(uint8_t c)
{
    if (c >= (uint8_t)'a' && c <= (uint8_t)'z') {
        return (uint8_t)(c - (uint8_t)'a' + (uint8_t)'A');
    }
    return c;
}

/* Printable ASCII minus the CP/M separators and wildcard characters. */
static int fs_name_char_ok(uint8_t c)
{
    if (c < 0x21u || c > 0x7Eu) {
        return 0;
    }
    switch (c) {
    case (uint8_t)'.':
    case (uint8_t)'*':
    case (uint8_t)'?':
    case (uint8_t)'/':
    case (uint8_t)'\\':
    case (uint8_t)':':
    case (uint8_t)';':
    case (uint8_t)',':
    case (uint8_t)'=':
    case (uint8_t)'<':
    case (uint8_t)'>':
    case (uint8_t)'[':
    case (uint8_t)']':
    case (uint8_t)'"':
    case (uint8_t)'|':
        return 0;
    default:
        return 1;
    }
}

static int fs_is_blank(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? 1 : 0;
}

/* "name.ext" (any case, at most 8 + 3 chars, extension optional)
 * -> 11-byte space-padded upper-case CP/M name. 0 ok, -1 malformed. */
static int fs_parse_name(const char *name, uint8_t out[11])
{
    size_t i = 0u;
    size_t j = 0u;

    if (name == NULL) {
        return -1;
    }
    memset(out, ' ', 11u);

    while (name[i] == ' ' || name[i] == '\t') {
        i++;
    }
    while (name[i] != '\0' && name[i] != '.' && !fs_is_blank(name[i])) {
        uint8_t c = fs_upper((uint8_t)name[i]);
        if (j >= 8u || !fs_name_char_ok(c)) {
            return -1;
        }
        out[j] = c;
        j++;
        i++;
    }
    if (j == 0u) {
        return -1;
    }
    if (name[i] == '.') {
        i++;
        j = 0u;
        while (name[i] != '\0' && !fs_is_blank(name[i])) {
            uint8_t c = fs_upper((uint8_t)name[i]);
            if (j >= 3u || !fs_name_char_ok(c)) {
                return -1;
            }
            out[8u + j] = c;
            j++;
            i++;
        }
    }
    while (fs_is_blank(name[i])) {
        i++;
    }
    if (name[i] != '\0') {
        return -1;
    }
    return 0;
}

/* ---- directory helpers ------------------------------------------------- */

static uint8_t *fs_entry(uint8_t disk, uint32_t index)
{
    return &g_img[disk][index * FS_DIR_ENTRY_SIZE];
}

/* Active = status byte 0x00 and a plausible first name character, so an
 * all-zero or all-0xE5 directory reads as empty. */
static int fs_entry_active(const uint8_t *e)
{
    return (e[FS_E_STATUS] == FS_ACTIVE && fs_name_char_ok(e[FS_E_NAME])) ? 1 : 0;
}

static uint32_t fs_entry_len(const uint8_t *e)
{
    return (uint32_t)e[FS_E_LEN_LO] | ((uint32_t)e[FS_E_LEN_HI] << 8);
}

static void fs_entry_set_len(uint8_t *e, uint32_t len)
{
    e[FS_E_LEN_LO] = (uint8_t)(len & 0xFFu);
    e[FS_E_LEN_HI] = (uint8_t)((len >> 8) & 0xFFu);
    e[FS_E_RECS] = (uint8_t)((len + TOS_DISK_SECTOR_BYTES - 1u) / TOS_DISK_SECTOR_BYTES);
}

/* Stored 11-byte name -> "NAME.EXT" (no dot when the extension is blank). */
static void fs_entry_name13(const uint8_t *e, char out[13])
{
    uint32_t p = 0u;
    uint32_t i;

    for (i = 0u; i < 8u; ++i) {
        uint8_t c = e[FS_E_NAME + i];
        if (c == (uint8_t)' ' || c == 0u) {
            break;
        }
        out[p++] = (char)fs_upper(c);
    }
    if (e[FS_E_EXT] != (uint8_t)' ' && e[FS_E_EXT] != 0u) {
        out[p++] = '.';
        for (i = 0u; i < 3u; ++i) {
            uint8_t c = e[FS_E_EXT + i];
            if (c == (uint8_t)' ' || c == 0u) {
                break;
            }
            out[p++] = (char)fs_upper(c);
        }
    }
    out[p] = '\0';
}

/* Case-insensitive compare of a stored name against a parsed (upper-case) one. */
static int fs_name_eq(const uint8_t *stored, const uint8_t cpm[11])
{
    uint32_t i;
    for (i = 0u; i < 11u; ++i) {
        if (fs_upper(stored[i]) != cpm[i]) {
            return 0;
        }
    }
    return 1;
}

static int fs_find(uint8_t disk, const uint8_t cpm[11])
{
    uint32_t i;
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *e = fs_entry(disk, i);
        if (!fs_entry_active(e)) {
            continue;
        }
        if (fs_name_eq(&e[FS_E_NAME], cpm)) {
            return (int)i;
        }
    }
    return -1;
}

static int fs_free_entry(uint8_t disk)
{
    uint32_t i;
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        if (!fs_entry_active(fs_entry(disk, i))) {
            return (int)i;
        }
    }
    return -1;
}

static uint8_t *fs_block_ptr(uint8_t disk, uint8_t block)
{
    return &g_img[disk][(uint32_t)block * FS_BLOCK_BYTES];
}

/* Lowest data block not referenced by any active entry, or -1 when the disk is full. */
static int fs_alloc_block(uint8_t disk)
{
    static uint8_t used[FS_BLOCK_COUNT];
    uint32_t i;
    uint32_t j;

    memset(used, 0, sizeof(used));
    used[0] = 1u;
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *e = fs_entry(disk, i);
        if (!fs_entry_active(e)) {
            continue;
        }
        for (j = 0u; j < FS_ENTRY_BLOCKS; ++j) {
            uint8_t b = e[FS_E_ALLOC + j];
            if (b != 0u && b < FS_BLOCK_COUNT) {
                used[b] = 1u;
            }
        }
    }
    for (i = 1u; i < FS_BLOCK_COUNT; ++i) {
        if (used[i] == 0u) {
            return (int)i;
        }
    }
    return -1;
}

/* Data blocks not referenced by any active entry. Block 0 is the directory. */
static uint32_t fs_free_blocks(uint8_t disk)
{
    static uint8_t used[FS_BLOCK_COUNT];
    uint32_t i;
    uint32_t j;
    uint32_t free_count = 0u;

    memset(used, 0, sizeof(used));
    used[0] = 1u;
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *e = fs_entry(disk, i);
        if (!fs_entry_active(e)) {
            continue;
        }
        for (j = 0u; j < FS_ENTRY_BLOCKS; ++j) {
            uint8_t b = e[FS_E_ALLOC + j];
            if (b != 0u && b < FS_BLOCK_COUNT) {
                used[b] = 1u;
            }
        }
    }
    for (i = 1u; i < FS_BLOCK_COUNT; ++i) {
        if (used[i] == 0u) {
            free_count++;
        }
    }
    return free_count;
}

/* Data blocks the entry at `idx` holds; they come back when it is deleted. */
static uint32_t fs_entry_block_count(uint8_t disk, uint32_t idx)
{
    const uint8_t *e = fs_entry(disk, idx);
    uint32_t j;
    uint32_t n = 0u;

    for (j = 0u; j < FS_ENTRY_BLOCKS; ++j) {
        if (e[FS_E_ALLOC + j] != 0u) {
            n++;
        }
    }
    return n;
}

static void fs_format_image(uint8_t disk)
{
    memset(&g_img[disk][0], (int)FS_DELETED, FS_DIR_BYTES);
    memset(&g_img[disk][FS_DIR_BYTES], 0, TOS_DISK_IMAGE_BYTES - FS_DIR_BYTES);
}

/* Copies up to `want` bytes of the file behind entry `e` starting at `pos`.
 * Stops at the end of the file or at a missing block. Returns bytes copied. */
static uint32_t fs_copy_out(uint8_t disk, const uint8_t *e, uint32_t pos, uint8_t *buf, uint32_t want)
{
    uint32_t flen = fs_entry_len(e);
    uint32_t total = 0u;

    while (total < want && pos < flen) {
        uint32_t slot = pos / FS_BLOCK_BYTES;
        uint32_t off = pos % FS_BLOCK_BYTES;
        uint32_t chunk = want - total;
        uint8_t block;

        if (slot >= FS_ENTRY_BLOCKS) {
            break;
        }
        block = e[FS_E_ALLOC + slot];
        if (block == 0u || block >= FS_BLOCK_COUNT) {
            break;
        }
        if (chunk > flen - pos) {
            chunk = flen - pos;
        }
        if (chunk > FS_BLOCK_BYTES - off) {
            chunk = FS_BLOCK_BYTES - off;
        }
        memcpy(buf + total, fs_block_ptr(disk, block) + off, (size_t)chunk);
        total += chunk;
        pos += chunk;
    }
    return total;
}

/* ---- open-table helpers ------------------------------------------------ */

static void fs_handle_clear(fs_handle_t *h)
{
    h->in_use = 0u;
    h->disk = 0u;
    h->dir_index = 0u;
    h->pad = 0u;
    h->pos = 0u;
}

static void fs_handles_reset(void)
{
    uint32_t i;
    for (i = 0u; i < FS_OPEN_MAX; ++i) {
        fs_handle_clear(&g_open[i]);
    }
}

/* Drop every handle on `disk`; dir_index >= 0 restricts it to that entry. */
static void fs_handles_drop(uint8_t disk, int dir_index)
{
    uint32_t i;
    for (i = 0u; i < FS_OPEN_MAX; ++i) {
        fs_handle_t *h = &g_open[i];
        if (h->in_use == 0u || h->disk != disk) {
            continue;
        }
        if (dir_index < 0 || h->dir_index == (uint8_t)dir_index) {
            fs_handle_clear(h);
        }
    }
}

/* Drop handles on `disk` whose directory entry is no longer active. */
static void fs_handles_prune(uint8_t disk)
{
    uint32_t i;
    for (i = 0u; i < FS_OPEN_MAX; ++i) {
        fs_handle_t *h = &g_open[i];
        if (h->in_use == 0u || h->disk != disk) {
            continue;
        }
        if (h->dir_index >= FS_DIR_ENTRIES || !fs_entry_active(fs_entry(disk, h->dir_index))) {
            fs_handle_clear(h);
        }
    }
}

static int fs_handle_alloc(uint8_t disk, uint32_t dir_index)
{
    uint32_t i;
    for (i = 0u; i < FS_OPEN_MAX; ++i) {
        fs_handle_t *h = &g_open[i];
        if (h->in_use == 0u) {
            h->in_use = 1u;
            h->disk = disk;
            h->dir_index = (uint8_t)dir_index;
            h->pad = 0u;
            h->pos = 0u;
            return (int)i;
        }
    }
    return -1;
}

/* Returns the handle's directory entry, or NULL when the handle is unusable. */
static uint8_t *fs_handle_entry(int fh)
{
    fs_handle_t *h;
    uint8_t *e;

    if (fh < 0 || fh >= (int)FS_OPEN_MAX) {
        return NULL;
    }
    h = &g_open[fh];
    if (h->in_use == 0u || h->disk >= g_disk_count || h->dir_index >= FS_DIR_ENTRIES) {
        return NULL;
    }
    e = fs_entry(h->disk, h->dir_index);
    if (!fs_entry_active(e)) {
        return NULL;
    }
    return e;
}

static void fs_delete_index(uint8_t disk, uint32_t index)
{
    uint8_t *e = fs_entry(disk, index);
    e[FS_E_STATUS] = FS_DELETED;
    e[FS_E_EXTENT] = 0u;
    fs_entry_set_len(e, 0u);
    memset(&e[FS_E_ALLOC], 0, FS_ENTRY_BLOCKS);
    fs_handles_drop(disk, (int)index);
    g_dirty[disk] = 1u;
}

static int fs_selected_ok(void)
{
    return (g_disk_count != 0u && g_selected < g_disk_count) ? 1 : 0;
}

/* ---- public: disks ----------------------------------------------------- */

int fs_init(uint8_t disks)
{
    uint32_t d;

    if (disks < 1u) {
        disks = 1u;
    }
    if (disks > TOS_DISKS_MAX) {
        disks = (uint8_t)TOS_DISKS_MAX;
    }
    g_disk_count = disks;
    g_selected = 0u;
    fs_handles_reset();
    for (d = 0u; d < TOS_DISKS_MAX; ++d) {
        g_dirty[d] = 0u;
    }
    for (d = 0u; d < disks; ++d) {
        int n = hal_disk_load((uint8_t)d, g_img[d], TOS_DISK_IMAGE_BYTES);
        if (n <= 0) {
            /* Missing image: blank formatted disk, dirty so fs_flush creates it. */
            fs_format_image((uint8_t)d);
            g_dirty[d] = 1u;
        } else if ((uint32_t)n < TOS_DISK_IMAGE_BYTES) {
            memset(&g_img[d][(uint32_t)n], 0, TOS_DISK_IMAGE_BYTES - (uint32_t)n);
        }
    }
    return 0;
}

int fs_select_disk(uint8_t disk)
{
    if (disk >= g_disk_count) {
        return -1;
    }
    g_selected = disk;
    return 0;
}

uint8_t fs_selected_disk(void)
{
    return g_selected;
}

uint8_t fs_disk_count(void)
{
    return g_disk_count;
}

uint8_t *fs_image_ptr(uint8_t disk)
{
    if (disk >= TOS_DISKS_MAX) {
        return NULL;
    }
    return g_img[disk];
}

uint32_t fs_image_size(void)
{
    return TOS_DISK_IMAGE_BYTES;
}

int fs_format(uint8_t disk)
{
    if (disk >= g_disk_count) {
        return -1;
    }
    fs_format_image(disk);
    fs_handles_drop(disk, -1);
    g_dirty[disk] = 1u;
    return 0;
}

void fs_reload(uint8_t disk)
{
    if (disk >= TOS_DISKS_MAX) {
        return;
    }
    /* The directory lives in the image buffer itself, so there is nothing to
     * re-parse; handles onto entries the host removed are dropped and the
     * image is marked dirty so the next fs_flush writes what the host put in. */
    fs_handles_prune(disk);
    if (disk < g_disk_count) {
        g_dirty[disk] = 1u;
    }
}

void fs_flush(void)
{
    uint32_t d;
    for (d = 0u; d < g_disk_count; ++d) {
        if (g_dirty[d] == 0u) {
            continue;
        }
        if (hal_disk_save((uint8_t)d, g_img[d], TOS_DISK_IMAGE_BYTES) == 0) {
            g_dirty[d] = 0u;
        }
    }
}

/* ---- public: files ----------------------------------------------------- */

int fs_open(const char *name)
{
    uint8_t cpm[11];
    int idx;

    if (!fs_selected_ok()) {
        return -1;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return -1;
    }
    idx = fs_find(g_selected, cpm);
    if (idx < 0) {
        return -1;
    }
    return fs_handle_alloc(g_selected, (uint32_t)idx);
}

int fs_create(const char *name)
{
    uint8_t cpm[11];
    int idx;
    int fh;
    uint8_t *e;

    if (!fs_selected_ok()) {
        return -1;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return -1;
    }
    if (fs_find(g_selected, cpm) >= 0) {
        return -1;
    }
    idx = fs_free_entry(g_selected);
    if (idx < 0) {
        return -1;
    }
    fh = fs_handle_alloc(g_selected, (uint32_t)idx);
    if (fh < 0) {
        return -1;
    }
    e = fs_entry(g_selected, (uint32_t)idx);
    memset(e, 0, FS_DIR_ENTRY_SIZE);
    e[FS_E_STATUS] = FS_ACTIVE;
    memcpy(&e[FS_E_NAME], cpm, 11u);
    e[FS_E_EXTENT] = 0u;
    fs_entry_set_len(e, 0u);
    g_dirty[g_selected] = 1u;
    return fh;
}

int fs_read(int fh, uint8_t *buf, int len)
{
    fs_handle_t *h;
    const uint8_t *e;
    uint32_t got;

    if (buf == NULL || len < 0) {
        return -1;
    }
    e = fs_handle_entry(fh);
    if (e == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    h = &g_open[fh];
    got = fs_copy_out(h->disk, e, h->pos, buf, (uint32_t)len);
    h->pos += got;
    return (int)got;
}

int fs_write(int fh, const uint8_t *buf, int len)
{
    fs_handle_t *h;
    uint8_t *e;
    uint32_t flen;
    uint32_t pos;
    uint32_t want;
    uint32_t total;

    if (buf == NULL || len < 0) {
        return -1;
    }
    e = fs_handle_entry(fh);
    if (e == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    h = &g_open[fh];
    flen = fs_entry_len(e);
    pos = h->pos;
    want = (uint32_t)len;
    total = 0u;
    while (total < want && pos < FS_FILE_MAX) {
        uint32_t slot = pos / FS_BLOCK_BYTES;
        uint32_t off = pos % FS_BLOCK_BYTES;
        uint32_t chunk = want - total;
        uint8_t block;

        if (slot >= FS_ENTRY_BLOCKS) {
            break;
        }
        block = e[FS_E_ALLOC + slot];
        if (block == 0u || block >= FS_BLOCK_COUNT) {
            int nb = fs_alloc_block(h->disk);
            if (nb < 0) {
                break;
            }
            block = (uint8_t)nb;
            e[FS_E_ALLOC + slot] = block;
            memset(fs_block_ptr(h->disk, block), 0, FS_BLOCK_BYTES);
        }
        if (chunk > FS_BLOCK_BYTES - off) {
            chunk = FS_BLOCK_BYTES - off;
        }
        if (chunk > FS_FILE_MAX - pos) {
            chunk = FS_FILE_MAX - pos;
        }
        memcpy(fs_block_ptr(h->disk, block) + off, buf + total, (size_t)chunk);
        g_dirty[h->disk] = 1u;
        total += chunk;
        pos += chunk;
    }
    if (pos > flen) {
        fs_entry_set_len(e, pos);
        g_dirty[h->disk] = 1u;
    }
    h->pos = pos;
    if (total == 0u) {
        return -1;
    }
    return (int)total;
}

void fs_close(int fh)
{
    if (fh < 0 || fh >= (int)FS_OPEN_MAX) {
        return;
    }
    fs_handle_clear(&g_open[fh]);
}

/* With names == NULL returns the number of files; otherwise fills up to `max`
 * names and returns how many were filled. */
int fs_list(char names[][13], int max)
{
    uint32_t i;
    int count = 0;
    int filled = 0;

    if (max < 0) {
        return -1;
    }
    if (!fs_selected_ok()) {
        return -1;
    }
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *e = fs_entry(g_selected, i);
        if (!fs_entry_active(e)) {
            continue;
        }
        if (names != NULL && filled < max) {
            fs_entry_name13(e, names[filled]);
            filled++;
        }
        count++;
    }
    return (names == NULL) ? count : filled;
}

int fs_delete(const char *name)
{
    uint8_t cpm[11];
    int idx;

    if (!fs_selected_ok()) {
        return -1;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return -1;
    }
    idx = fs_find(g_selected, cpm);
    if (idx < 0) {
        return -1;
    }
    fs_delete_index(g_selected, (uint32_t)idx);
    return 0;
}

int fs_exists(const char *name)
{
    uint8_t cpm[11];

    if (!fs_selected_ok()) {
        return 0;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return 0;
    }
    return (fs_find(g_selected, cpm) >= 0) ? 1 : 0;
}

/* ---- public: raw sectors (selected disk; sectors are 1-based) ---------- */

static int fs_sector_offset(uint8_t track, uint8_t sector, uint32_t *off)
{
    if (track >= TOS_DISK_TRACKS || sector == 0u || sector > TOS_DISK_SECTORS) {
        return -1;
    }
    *off = ((uint32_t)track * TOS_DISK_SECTORS + (uint32_t)(sector - 1u)) * TOS_DISK_SECTOR_BYTES;
    return 0;
}

int fs_read_sector(uint8_t track, uint8_t sector, uint8_t *buf)
{
    uint32_t off;

    if (buf == NULL || !fs_selected_ok()) {
        return -1;
    }
    if (fs_sector_offset(track, sector, &off) != 0) {
        return -1;
    }
    memcpy(buf, &g_img[g_selected][off], TOS_DISK_SECTOR_BYTES);
    return 0;
}

int fs_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf)
{
    uint32_t off;

    if (buf == NULL || !fs_selected_ok()) {
        return -1;
    }
    if (fs_sector_offset(track, sector, &off) != 0) {
        return -1;
    }
    memcpy(&g_img[g_selected][off], buf, TOS_DISK_SECTOR_BYTES);
    g_dirty[g_selected] = 1u;
    if (off < FS_DIR_BYTES) {
        /* A program rewrote part of the directory: drop handles onto entries that vanished. */
        fs_handles_prune(g_selected);
    }
    return 0;
}

/* ---- public: whole-file convenience ----------------------------------- */

int fs_put_file(const char *name, const uint8_t *data, uint32_t len)
{
    uint8_t cpm[11];
    int idx;
    int fh;
    uint32_t done;

    if (!fs_selected_ok()) {
        return -1;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return -1;
    }
    if (len > FS_FILE_MAX) {
        return -1;
    }
    if (data == NULL && len > 0u) {
        return -1;
    }
    idx = fs_find(g_selected, cpm);
    if (idx >= 0) {
        /* Overwriting must not destroy the file it replaces: check the new contents fit in the
           free space plus the blocks this entry will give back, before deleting anything. */
        const uint32_t need = (len + FS_BLOCK_BYTES - 1u) / FS_BLOCK_BYTES;
        const uint32_t have = fs_free_blocks(g_selected) + fs_entry_block_count(g_selected, (uint32_t)idx);
        if (need > have) {
            return -1;
        }
        fs_delete_index(g_selected, (uint32_t)idx);
    }
    fh = fs_create(name);
    if (fh < 0) {
        return -1;
    }
    done = 0u;
    while (done < len) {
        uint32_t chunk = len - done;
        int n;
        if (chunk > 16384u) {
            chunk = 16384u;
        }
        n = fs_write(fh, data + done, (int)chunk);
        if (n <= 0) {
            /* Disk full mid-way: do not leave a truncated file behind. */
            fs_close(fh);
            idx = fs_find(g_selected, cpm);
            if (idx >= 0) {
                fs_delete_index(g_selected, (uint32_t)idx);
            }
            return -1;
        }
        done += (uint32_t)n;
    }
    fs_close(fh);
    return 0;
}

/* Returns the file length after copying it into buf; -1 when the file is
 * missing or does not fit in `cap` (nothing is copied in that case). */
int fs_get_file(const char *name, uint8_t *buf, uint32_t cap)
{
    uint8_t cpm[11];
    int idx;
    const uint8_t *e;
    uint32_t flen;
    uint32_t got;

    if (!fs_selected_ok()) {
        return -1;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return -1;
    }
    idx = fs_find(g_selected, cpm);
    if (idx < 0) {
        return -1;
    }
    e = fs_entry(g_selected, (uint32_t)idx);
    flen = fs_entry_len(e);
    if (flen == 0u) {
        return 0;
    }
    if (buf == NULL || flen > cap) {
        return -1;
    }
    got = fs_copy_out(g_selected, e, 0u, buf, flen);
    return (int)got;
}

int fs_file_size(const char *name)
{
    uint8_t cpm[11];
    int idx;

    if (!fs_selected_ok()) {
        return -1;
    }
    if (fs_parse_name(name, cpm) != 0) {
        return -1;
    }
    idx = fs_find(g_selected, cpm);
    if (idx < 0) {
        return -1;
    }
    return (int)fs_entry_len(fs_entry(g_selected, (uint32_t)idx));
}

/* ---- public: snapshot state (open table + selection, never images) ---- */

uint32_t fs_state_size(void)
{
    return FS_STATE_BYTES;
}

void fs_state_save(uint8_t *buf)
{
    uint32_t i;

    if (buf == NULL) {
        return;
    }
    memset(buf, 0, FS_STATE_BYTES);
    buf[0] = g_disk_count;
    buf[1] = g_selected;
    buf[2] = 0u;
    buf[3] = 0u;
    for (i = 0u; i < FS_OPEN_MAX; ++i) {
        uint8_t *p = &buf[4u + i * 8u];
        const fs_handle_t *h = &g_open[i];
        p[0] = h->in_use;
        p[1] = h->disk;
        p[2] = h->dir_index;
        p[3] = 0u;
        p[4] = (uint8_t)(h->pos & 0xFFu);
        p[5] = (uint8_t)((h->pos >> 8) & 0xFFu);
        p[6] = (uint8_t)((h->pos >> 16) & 0xFFu);
        p[7] = (uint8_t)((h->pos >> 24) & 0xFFu);
    }
}

void fs_state_load(const uint8_t *buf)
{
    uint32_t i;

    if (buf == NULL) {
        return;
    }
    /* The disk count is machine configuration, which a snapshot never changes;
     * only the selection and the open table are restored. */
    g_selected = (buf[1] < g_disk_count) ? buf[1] : 0u;
    for (i = 0u; i < FS_OPEN_MAX; ++i) {
        const uint8_t *p = &buf[4u + i * 8u];
        fs_handle_t *h = &g_open[i];
        h->in_use = (p[0] != 0u) ? 1u : 0u;
        h->disk = p[1];
        h->dir_index = p[2];
        h->pad = 0u;
        h->pos = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        if (h->in_use != 0u && (h->disk >= g_disk_count || h->dir_index >= FS_DIR_ENTRIES)) {
            fs_handle_clear(h);
        }
    }
}
