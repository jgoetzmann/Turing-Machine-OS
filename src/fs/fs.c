#include "fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#define FS_TRACKS 77u
#define FS_SECTORS_PER_TRACK 26u
#define FS_BYTES_PER_SECTOR 256u
#define FS_TOTAL_BYTES (FS_TRACKS * FS_SECTORS_PER_TRACK * FS_BYTES_PER_SECTOR)
#define FS_DIR_ENTRY_SIZE 32u
#define FS_DIR_ENTRIES 64u
#define FS_DIR_BYTES (FS_DIR_ENTRY_SIZE * FS_DIR_ENTRIES)
#define FS_OPEN_MAX 16u

static FILE *g_disk_fp = NULL;
static uint8_t g_dir_raw[FS_DIR_BYTES];
static uint8_t g_dir_active[FS_DIR_ENTRIES];
static uint8_t g_open_dir_index[FS_OPEN_MAX];
static uint8_t g_open_in_use[FS_OPEN_MAX];
static uint16_t g_open_pos[FS_OPEN_MAX];
static int g_dirty = 0;

static long fs_sector_offset(uint8_t track, uint8_t sector) {
    unsigned long linear_sector = ((unsigned long)track * FS_SECTORS_PER_TRACK) +
                                  ((unsigned long)sector - 1u);
    return (long)(linear_sector * FS_BYTES_PER_SECTOR);
}

static void fs_reset_open_table(void) {
    unsigned int i;
    for (i = 0; i < FS_OPEN_MAX; ++i) {
        g_open_in_use[i] = 0u;
        g_open_dir_index[i] = 0u;
        g_open_pos[i] = 0u;
    }
}

static int fs_linear_to_track_sector(uint16_t linear, uint8_t *track, uint8_t *sector) {
    if (track == NULL || sector == NULL) {
        return -1;
    }
    if (linear >= (uint16_t)(FS_TRACKS * FS_SECTORS_PER_TRACK)) {
        return -1;
    }
    *track = (uint8_t)(linear / FS_SECTORS_PER_TRACK);
    *sector = (uint8_t)((linear % FS_SECTORS_PER_TRACK) + 1u);
    return 0;
}

static uint16_t fs_track_sector_to_linear(uint8_t track, uint8_t sector) {
    return (uint16_t)((uint16_t)track * FS_SECTORS_PER_TRACK + (uint16_t)(sector - 1u));
}

static int fs_build_cpm_name(const char *name, uint8_t out_name[11]) {
    unsigned int i = 0u;
    unsigned int j = 0u;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    for (i = 0u; i < 11u; ++i) {
        out_name[i] = ' ';
    }

    i = 0u;
    while (name[i] != '\0' && name[i] != '.') {
        unsigned char ch = (unsigned char)name[i];
        if (j >= 8u || !isalnum(ch)) {
            return -1;
        }
        out_name[j++] = (uint8_t)toupper(ch);
        i++;
    }
    if (j == 0u) {
        return -1;
    }
    if (name[i] == '.') {
        i++;
        j = 0u;
        while (name[i] != '\0') {
            unsigned char ch = (unsigned char)name[i];
            if (j >= 3u || !isalnum(ch)) {
                return -1;
            }
            out_name[8u + j] = (uint8_t)toupper(ch);
            j++;
            i++;
        }
        if (j == 0u) {
            return -1;
        }
    }
    return 0;
}

static int fs_refresh_directory(void) {
    unsigned int i;
    if (g_disk_fp == NULL) {
        return -1;
    }
    if (fseek(g_disk_fp, 0L, SEEK_SET) != 0) {
        return -1;
    }
    if (fread(g_dir_raw, 1u, FS_DIR_BYTES, g_disk_fp) != FS_DIR_BYTES) {
        return -1;
    }
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        uint8_t status = g_dir_raw[i * FS_DIR_ENTRY_SIZE];
        g_dir_active[i] = (status == 0x00u) ? 1u : 0u;
    }
    return 0;
}

static int fs_persist_directory_entry(uint8_t dir_index) {
    long offset;
    if (g_disk_fp == NULL || dir_index >= FS_DIR_ENTRIES) {
        return -1;
    }
    offset = (long)dir_index * FS_DIR_ENTRY_SIZE;
    if (fseek(g_disk_fp, offset, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(&g_dir_raw[offset], 1u, FS_DIR_ENTRY_SIZE, g_disk_fp) != FS_DIR_ENTRY_SIZE) {
        return -1;
    }
    g_dirty = 1;
    return 0;
}

static int fs_sector_in_use(uint16_t linear_sector) {
    unsigned int i;
    unsigned int j;
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *entry = &g_dir_raw[i * FS_DIR_ENTRY_SIZE];
        if (g_dir_active[i] == 0u) {
            continue;
        }
        for (j = 0u; j < 16u; ++j) {
            uint8_t alloc = entry[16u + j];
            if (alloc == 0u) {
                continue;
            }
            if ((uint16_t)(alloc - 1u) == linear_sector) {
                return 1;
            }
        }
    }
    return 0;
}

static int fs_alloc_free_sector(uint8_t *track, uint8_t *sector) {
    uint16_t linear;
    for (linear = 8u; linear < (uint16_t)(FS_TRACKS * FS_SECTORS_PER_TRACK); ++linear) {
        if (fs_sector_in_use(linear) == 0) {
            return fs_linear_to_track_sector(linear, track, sector);
        }
    }
    return -1;
}

static int fs_find_dir_index_by_cpm_name(const uint8_t cpm_name[11]) {
    unsigned int i;
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *entry = &g_dir_raw[i * FS_DIR_ENTRY_SIZE];
        if (g_dir_active[i] == 0u) {
            continue;
        }
        if (memcmp(&entry[1], cpm_name, 11u) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void fs_entry_to_name13(const uint8_t *entry, char out[13]) {
    unsigned int p = 0u;
    unsigned int i;
    for (i = 0u; i < 8u; ++i) {
        if (entry[1u + i] == ' ') {
            break;
        }
        out[p++] = (char)entry[1u + i];
    }
    if (entry[9] != ' ') {
        out[p++] = '.';
        for (i = 0u; i < 3u; ++i) {
            if (entry[9u + i] == ' ') {
                break;
            }
            out[p++] = (char)entry[9u + i];
        }
    }
    out[p] = '\0';
}

int fs_init(const char *disk_image_path) {
    FILE *fp;
    long size;

    if (disk_image_path == NULL) {
        return -1;
    }

    fp = fopen(disk_image_path, "r+b");
    if (fp == NULL) {
        return -1;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        (void)fclose(fp);
        return -1;
    }
    size = ftell(fp);
    if (size != (long)FS_TOTAL_BYTES) {
        (void)fclose(fp);
        return -1;
    }
    if (fseek(fp, 0L, SEEK_SET) != 0) {
        (void)fclose(fp);
        return -1;
    }

    if (g_disk_fp != NULL) {
        (void)fclose(g_disk_fp);
    }
    g_disk_fp = fp;
    g_dirty = 0;
    fs_reset_open_table();
    if (fs_refresh_directory() != 0) {
        (void)fclose(g_disk_fp);
        g_disk_fp = NULL;
        return -1;
    }
    return 0;
}

int fs_open(const char *name) {
    uint8_t wanted[11];
    unsigned int h;
    int dir_index;

    if (g_disk_fp == NULL) {
        return -1;
    }
    if (fs_build_cpm_name(name, wanted) != 0) {
        return -1;
    }
    if (fs_refresh_directory() != 0) {
        return -1;
    }

    dir_index = fs_find_dir_index_by_cpm_name(wanted);
    if (dir_index < 0) {
        return -1;
    }
    for (h = 0u; h < FS_OPEN_MAX; ++h) {
        if (g_open_in_use[h] == 0u) {
            g_open_in_use[h] = 1u;
            g_open_dir_index[h] = (uint8_t)dir_index;
            g_open_pos[h] = 0u;
            return (int)h;
        }
    }
    return -1;
}

int fs_read(int fh, uint8_t *buf, int len) {
    uint8_t *entry;
    uint16_t pos;
    uint16_t size_bytes;
    int total = 0;
    uint8_t sector_buf[FS_BYTES_PER_SECTOR];

    if (fh < 0 || fh >= (int)FS_OPEN_MAX || buf == NULL || len < 0) {
        return -1;
    }
    if (g_open_in_use[(unsigned int)fh] == 0u) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    if (fs_refresh_directory() != 0) {
        return -1;
    }

    entry = &g_dir_raw[g_open_dir_index[(unsigned int)fh] * FS_DIR_ENTRY_SIZE];
    size_bytes = (uint16_t)entry[15] * FS_BYTES_PER_SECTOR;
    pos = g_open_pos[(unsigned int)fh];
    if (pos >= size_bytes) {
        return 0;
    }

    while (total < len && pos < size_bytes) {
        uint8_t slot = (uint8_t)(pos / FS_BYTES_PER_SECTOR);
        uint8_t in_sector = (uint8_t)(pos % FS_BYTES_PER_SECTOR);
        uint8_t alloc = entry[16u + slot];
        uint8_t track;
        uint8_t sector;
        int chunk;
        int left_file = (int)(size_bytes - pos);
        int left_sector = (int)(FS_BYTES_PER_SECTOR - in_sector);

        if (slot >= 16u || alloc == 0u) {
            break;
        }
        if (fs_linear_to_track_sector((uint16_t)(alloc - 1u), &track, &sector) != 0) {
            break;
        }
        if (fs_read_sector(track, sector, sector_buf) != 0) {
            return (total > 0) ? total : -1;
        }

        chunk = len - total;
        if (chunk > left_file) {
            chunk = left_file;
        }
        if (chunk > left_sector) {
            chunk = left_sector;
        }
        memcpy(&buf[total], &sector_buf[in_sector], (size_t)chunk);
        total += chunk;
        pos = (uint16_t)(pos + (uint16_t)chunk);
    }

    g_open_pos[(unsigned int)fh] = pos;
    return total;
}

int fs_write(int fh, const uint8_t *buf, int len) {
    uint8_t *entry;
    uint16_t pos;
    int total = 0;
    uint8_t sector_buf[FS_BYTES_PER_SECTOR];

    if (fh < 0 || fh >= (int)FS_OPEN_MAX || buf == NULL || len < 0) {
        return -1;
    }
    if (g_open_in_use[(unsigned int)fh] == 0u) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    if (fs_refresh_directory() != 0) {
        return -1;
    }

    entry = &g_dir_raw[g_open_dir_index[(unsigned int)fh] * FS_DIR_ENTRY_SIZE];
    pos = g_open_pos[(unsigned int)fh];

    while (total < len) {
        uint8_t slot = (uint8_t)(pos / FS_BYTES_PER_SECTOR);
        uint8_t in_sector = (uint8_t)(pos % FS_BYTES_PER_SECTOR);
        uint8_t alloc = 0u;
        uint8_t track;
        uint8_t sector;
        int chunk = len - total;

        if (slot >= 16u) {
            break;
        }

        alloc = entry[16u + slot];
        if (alloc == 0u) {
            if (fs_alloc_free_sector(&track, &sector) != 0) {
                break;
            }
            alloc = (uint8_t)(fs_track_sector_to_linear(track, sector) + 1u);
            entry[16u + slot] = alloc;
            memset(sector_buf, 0, sizeof(sector_buf));
        } else {
            if (fs_linear_to_track_sector((uint16_t)(alloc - 1u), &track, &sector) != 0) {
                return (total > 0) ? total : -1;
            }
            if (fs_read_sector(track, sector, sector_buf) != 0) {
                return (total > 0) ? total : -1;
            }
        }

        if (chunk > (int)(FS_BYTES_PER_SECTOR - in_sector)) {
            chunk = (int)(FS_BYTES_PER_SECTOR - in_sector);
        }
        memcpy(&sector_buf[in_sector], &buf[total], (size_t)chunk);
        if (fs_write_sector(track, sector, sector_buf) != 0) {
            return (total > 0) ? total : -1;
        }
        total += chunk;
        pos = (uint16_t)(pos + (uint16_t)chunk);
    }

    if (pos > (uint16_t)entry[15] * FS_BYTES_PER_SECTOR) {
        uint8_t sectors_used = (uint8_t)((pos + (FS_BYTES_PER_SECTOR - 1u)) / FS_BYTES_PER_SECTOR);
        entry[15] = sectors_used;
    }

    g_open_pos[(unsigned int)fh] = pos;
    if (fs_persist_directory_entry(g_open_dir_index[(unsigned int)fh]) != 0) {
        return (total > 0) ? total : -1;
    }
    return total;
}

void fs_close(int fh) {
    if (fh < 0 || fh >= (int)FS_OPEN_MAX) {
        return;
    }
    g_open_in_use[(unsigned int)fh] = 0u;
    g_open_dir_index[(unsigned int)fh] = 0u;
    g_open_pos[(unsigned int)fh] = 0u;
}

int fs_list(char names[][13], int max) {
    unsigned int i;
    int count = 0;
    if (max < 0) {
        return -1;
    }
    if (g_disk_fp == NULL) {
        return -1;
    }
    if (fs_refresh_directory() != 0) {
        return -1;
    }
    for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
        const uint8_t *entry = &g_dir_raw[i * FS_DIR_ENTRY_SIZE];
        if (g_dir_active[i] == 0u) {
            continue;
        }
        if (count < max) {
            fs_entry_to_name13(entry, names[count]);
        }
        count++;
    }
    return count;
}

int fs_delete(const char *name) {
    uint8_t wanted[11];
    int dir_index;
    unsigned int h;
    uint8_t *entry;

    if (g_disk_fp == NULL) {
        return -1;
    }
    if (fs_build_cpm_name(name, wanted) != 0) {
        return -1;
    }
    if (fs_refresh_directory() != 0) {
        return -1;
    }
    dir_index = fs_find_dir_index_by_cpm_name(wanted);
    if (dir_index < 0) {
        return -1;
    }

    entry = &g_dir_raw[(unsigned int)dir_index * FS_DIR_ENTRY_SIZE];
    entry[0] = 0xE5u;
    memset(&entry[16], 0, 16u);
    entry[15] = 0u;
    if (fs_persist_directory_entry((uint8_t)dir_index) != 0) {
        return -1;
    }
    g_dir_active[(unsigned int)dir_index] = 0u;

    for (h = 0u; h < FS_OPEN_MAX; ++h) {
        if (g_open_in_use[h] != 0u && g_open_dir_index[h] == (uint8_t)dir_index) {
            g_open_in_use[h] = 0u;
            g_open_dir_index[h] = 0u;
            g_open_pos[h] = 0u;
        }
    }
    return 0;
}

int fs_exists(const char *name) {
    uint8_t wanted[11];
    if (g_disk_fp == NULL) {
        return 0;
    }
    if (fs_build_cpm_name(name, wanted) != 0) {
        return 0;
    }
    if (fs_refresh_directory() != 0) {
        return 0;
    }
    return (fs_find_dir_index_by_cpm_name(wanted) >= 0) ? 1 : 0;
}

void fs_flush(void) {
    if (g_disk_fp != NULL && g_dirty != 0) {
        if (fflush(g_disk_fp) == 0) {
            g_dirty = 0;
        }
    }
}

int fs_read_sector(uint8_t track, uint8_t sector, uint8_t *buf) {
    long offset;
    if (g_disk_fp == NULL || buf == NULL) {
        return -1;
    }
    if (track >= FS_TRACKS || sector == 0u || sector > FS_SECTORS_PER_TRACK) {
        return -1;
    }

    offset = fs_sector_offset(track, sector);
    if (fseek(g_disk_fp, offset, SEEK_SET) != 0) {
        return -1;
    }
    if (fread(buf, 1u, FS_BYTES_PER_SECTOR, g_disk_fp) != FS_BYTES_PER_SECTOR) {
        return -1;
    }
    return 0;
}

int fs_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf) {
    long offset;
    if (g_disk_fp == NULL || buf == NULL) {
        return -1;
    }
    if (track >= FS_TRACKS || sector == 0u || sector > FS_SECTORS_PER_TRACK) {
        return -1;
    }

    offset = fs_sector_offset(track, sector);
    if (fseek(g_disk_fp, offset, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(buf, 1u, FS_BYTES_PER_SECTOR, g_disk_fp) != FS_BYTES_PER_SECTOR) {
        return -1;
    }
    g_dirty = 1;
    return 0;
}
