#ifndef TURINGOS_FS_H
#define TURINGOS_FS_H

#include <stdint.h>

int fs_init(const char *disk_image_path);
int fs_open(const char *name);
int fs_read(int fh, uint8_t *buf, int len);
int fs_write(int fh, const uint8_t *buf, int len);
void fs_close(int fh);
int fs_list(char names[][13], int max);
int fs_delete(const char *name);
int fs_exists(const char *name);
void fs_flush(void);

#endif
