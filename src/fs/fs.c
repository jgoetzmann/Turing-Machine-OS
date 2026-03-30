#include "fs.h"

#include <stdio.h>
#include <stddef.h>

#define FS_TRACKS 77u
#define FS_SECTORS_PER_TRACK 26u
#define FS_BYTES_PER_SECTOR 256u
#define FS_TOTAL_BYTES (FS_TRACKS * FS_SECTORS_PER_TRACK * FS_BYTES_PER_SECTOR)

static FILE *g_disk_fp = NULL;

static long fs_sector_offset(uint8_t track, uint8_t sector) {
    unsigned long linear_sector = ((unsigned long)track * FS_SECTORS_PER_TRACK) +
                                  ((unsigned long)sector - 1u);
    return (long)(linear_sector * FS_BYTES_PER_SECTOR);
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
    return 0;
}

int fs_open(const char *name) {
    (void)name;
    return -1;
}

int fs_read(int fh, uint8_t *buf, int len) {
    (void)fh;
    (void)buf;
    (void)len;
    return -1;
}

int fs_write(int fh, const uint8_t *buf, int len) {
    (void)fh;
    (void)buf;
    (void)len;
    return -1;
}

void fs_close(int fh) {
    (void)fh;
}

int fs_list(char names[][13], int max) {
    (void)names;
    (void)max;
    return 0;
}

int fs_delete(const char *name) {
    (void)name;
    return -1;
}

int fs_exists(const char *name) {
    (void)name;
    return 0;
}

void fs_flush(void) {
    if (g_disk_fp != NULL) {
        (void)fflush(g_disk_fp);
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
    if (fflush(g_disk_fp) != 0) {
        return -1;
    }
    return 0;
}
