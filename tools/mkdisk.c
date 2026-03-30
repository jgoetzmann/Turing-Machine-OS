#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    enum {
        TRACKS = 77,
        SECTORS_PER_TRACK = 26,
        BYTES_PER_SECTOR = 256,
        DIR_ENTRIES = 64,
        DIR_ENTRY_SIZE = 32
    };

    FILE *fp = fopen("disk.img", "wb");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    uint8_t zero_sector[BYTES_PER_SECTOR];
    memset(zero_sector, 0, sizeof(zero_sector));

    uint8_t dir_buf[DIR_ENTRIES * DIR_ENTRY_SIZE];
    memset(dir_buf, 0xE5, sizeof(dir_buf));

    if (fwrite(dir_buf, 1, sizeof(dir_buf), fp) != sizeof(dir_buf)) {
        perror("fwrite");
        fclose(fp);
        return 1;
    }

    const long total_bytes = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR;
    const long written = (long)sizeof(dir_buf);
    const long remaining = total_bytes - written;

    for (long i = 0; i < remaining; i += BYTES_PER_SECTOR) {
        if (fwrite(zero_sector, 1, sizeof(zero_sector), fp) != sizeof(zero_sector)) {
            perror("fwrite");
            fclose(fp);
            return 1;
        }
    }

    if (fclose(fp) != 0) {
        perror("fclose");
        return 1;
    }

    printf("Created disk.img (%ld bytes)\n", total_bytes);
    return 0;
}
