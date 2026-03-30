#include "../../src/fs/fs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DISK_PATH "build/tests/fs/test_disk.img"
#define TRACKS 77u
#define SECTORS_PER_TRACK 26u
#define BYTES_PER_SECTOR 256u
#define TOTAL_BYTES (TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR)

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void create_blank_disk(const char *path) {
    FILE *fp = fopen(path, "wb");
    uint8_t zero[BYTES_PER_SECTOR];
    unsigned long sectors = TRACKS * SECTORS_PER_TRACK;
    unsigned long i;
    if (fp == NULL) {
        fail("cannot create test disk");
    }
    memset(zero, 0, sizeof(zero));
    for (i = 0; i < sectors; ++i) {
        if (fwrite(zero, 1u, sizeof(zero), fp) != sizeof(zero)) {
            fclose(fp);
            fail("cannot populate test disk");
        }
    }
    if (fclose(fp) != 0) {
        fail("cannot close test disk");
    }
}

int main(void) {
    uint8_t write_buf[BYTES_PER_SECTOR];
    uint8_t read_buf[BYTES_PER_SECTOR];
    FILE *bad_fp;
    const char *bad_path = "build/tests/fs/bad_disk.img";
    unsigned int i;

    create_blank_disk(TEST_DISK_PATH);
    if (fs_init(TEST_DISK_PATH) != 0) {
        fail("fs_init should accept valid disk geometry");
    }

    for (i = 0; i < BYTES_PER_SECTOR; ++i) {
        write_buf[i] = (uint8_t)(i ^ 0x5Au);
    }
    if (fs_write_sector(3u, 7u, write_buf) != 0) {
        fail("fs_write_sector should succeed on valid track/sector");
    }
    memset(read_buf, 0, sizeof(read_buf));
    if (fs_read_sector(3u, 7u, read_buf) != 0) {
        fail("fs_read_sector should succeed on valid track/sector");
    }
    if (memcmp(write_buf, read_buf, sizeof(write_buf)) != 0) {
        fail("sector round-trip mismatch");
    }

    if (fs_read_sector(77u, 1u, read_buf) == 0) {
        fail("fs_read_sector should reject out-of-range track");
    }
    if (fs_read_sector(0u, 0u, read_buf) == 0) {
        fail("fs_read_sector should reject sector 0");
    }
    if (fs_write_sector(0u, 27u, write_buf) == 0) {
        fail("fs_write_sector should reject out-of-range sector");
    }

    bad_fp = fopen(bad_path, "wb");
    if (bad_fp == NULL) {
        fail("cannot create bad geometry disk");
    }
    if (fwrite(write_buf, 1u, BYTES_PER_SECTOR, bad_fp) != BYTES_PER_SECTOR) {
        fclose(bad_fp);
        fail("cannot write bad geometry disk");
    }
    if (fclose(bad_fp) != 0) {
        fail("cannot close bad geometry disk");
    }
    if (fs_init(bad_path) == 0) {
        fail("fs_init should reject bad geometry");
    }

    (void)TOTAL_BYTES;
    puts("PASS: test_fs_sector");
    return 0;
}
