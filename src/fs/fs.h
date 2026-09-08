#ifndef TURINGOS_FS_H
#define TURINGOS_FS_H
#include <stdint.h>
/* CP/M-style flat filesystem over up to 2 disk images held in static buffers (loaded/saved through the HAL).
 * Names are 8.3 ("NAME.EXT"), case-insensitive, stored upper-case. All file ops act on the selected disk. */
int      fs_init(uint8_t disks);                       /* 1|2; loads each image via hal_disk_load; missing image => blank formatted image; returns 0 */
int      fs_select_disk(uint8_t disk);                 /* 0 ok, -1 if disk >= count */
uint8_t  fs_selected_disk(void);
uint8_t  fs_disk_count(void);
uint8_t *fs_image_ptr(uint8_t disk);                   /* TOS_DISK_IMAGE_BYTES bytes */
uint32_t fs_image_size(void);                          /* TOS_DISK_IMAGE_BYTES */
int      fs_format(uint8_t disk);                      /* blank directory (0xE5 entries), zero data */
void     fs_reload(uint8_t disk);                      /* re-parse the directory from the image buffer (after host writes into it) */
int      fs_open(const char *name);                    /* handle 0..15 or -1 */
int      fs_create(const char *name);                  /* handle or -1; fails if exists */
int      fs_read(int fh, uint8_t *buf, int len);       /* bytes read, 0 at end, -1 error */
int      fs_write(int fh, const uint8_t *buf, int len);/* bytes written or -1 */
void     fs_close(int fh);
int      fs_list(char names[][13], int max);           /* count or -1 */
int      fs_delete(const char *name);                  /* 0 ok, -1 missing */
int      fs_exists(const char *name);
void     fs_flush(void);                               /* hal_disk_save for dirty images */
int      fs_read_sector(uint8_t track, uint8_t sector, uint8_t *buf);
int      fs_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf);
int      fs_put_file(const char *name, const uint8_t *data, uint32_t len);  /* create or overwrite; 0 ok */
int      fs_get_file(const char *name, uint8_t *buf, uint32_t cap);         /* length or -1 */
int      fs_file_size(const char *name);                                    /* bytes or -1 */
uint32_t fs_state_size(void);                          /* open table + directory cache, not images */
void     fs_state_save(uint8_t *buf);
void     fs_state_load(const uint8_t *buf);
#endif
