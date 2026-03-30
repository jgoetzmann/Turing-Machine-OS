#include "fs.h"

#include <stddef.h>

int fs_init(const char *disk_image_path) {
    (void)disk_image_path;
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
}
