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

static void write_dir_entry(FILE *fp,
                            unsigned int entry_idx,
                            const char *name8,
                            const char *ext3) {
    uint8_t entry[32];
    if (entry_idx >= 64u) {
        fail("entry index out of range");
    }
    memset(entry, 0, sizeof(entry));
    entry[0] = 0x00u;
    memset(&entry[1], ' ', 8u);
    memset(&entry[9], ' ', 3u);
    memcpy(&entry[1], name8, strlen(name8));
    memcpy(&entry[9], ext3, strlen(ext3));
    if (fseek(fp, (long)(entry_idx * 32u), SEEK_SET) != 0) {
        fail("cannot seek to directory entry");
    }
    if (fwrite(entry, 1u, sizeof(entry), fp) != sizeof(entry)) {
        fail("cannot write directory entry");
    }
}

int main(void) {
    uint8_t write_buf[BYTES_PER_SECTOR];
    uint8_t read_buf[BYTES_PER_SECTOR];
    uint8_t file_write[700];
    uint8_t file_read[700];
    FILE *bad_fp;
    FILE *fp;
    const char *bad_path = "build/tests/fs/bad_disk.img";
    int h1;
    int h2;
    int hw;
    int hr;
    char names[8][13];
    int listed;
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

    /* Build a disk with active directory entries and verify fs_open. */
    create_blank_disk(TEST_DISK_PATH);
    fp = fopen(TEST_DISK_PATH, "r+b");
    if (fp == NULL) {
        fail("cannot open test disk for directory setup");
    }
    write_dir_entry(fp, 0u, "HELLO", "COM");
    write_dir_entry(fp, 1u, "README", "TXT");
    if (fclose(fp) != 0) {
        fail("cannot close directory setup disk");
    }
    if (fs_init(TEST_DISK_PATH) != 0) {
        fail("fs_init should succeed after directory setup");
    }

    h1 = fs_open("HELLO.COM");
    if (h1 < 0 || h1 > 15) {
        fail("fs_open should return valid handle for HELLO.COM");
    }
    h2 = fs_open("readme.txt");
    if (h2 < 0 || h2 > 15) {
        fail("fs_open should be case-insensitive through uppercase normalization");
    }
    if (fs_open("MISSING.BIN") != -1) {
        fail("fs_open should fail for missing file");
    }
    if (fs_open("BAD-NAME.TXT") != -1) {
        fail("fs_open should reject invalid CP/M-style name");
    }
    if (fs_exists("HELLO.COM") != 1) {
        fail("fs_exists should detect existing file");
    }
    if (fs_exists("missing.bin") != 0) {
        fail("fs_exists should return 0 for missing file");
    }

    listed = fs_list(names, 8);
    if (listed < 2) {
        fail("fs_list should include active directory entries");
    }
    if (strcmp(names[0], "HELLO.COM") != 0 || strcmp(names[1], "README.TXT") != 0) {
        fail("fs_list should expose normalized 8.3 names in directory order");
    }

    fs_close(h1);
    fs_close(h2);

    /* Sequential fs_write/fs_read via allocation map. */
    fp = fopen(TEST_DISK_PATH, "r+b");
    if (fp == NULL) {
        fail("cannot reopen disk for DATA.BIN directory setup");
    }
    write_dir_entry(fp, 2u, "DATA", "BIN");
    if (fclose(fp) != 0) {
        fail("cannot close disk before write/read test");
    }
    if (fs_init(TEST_DISK_PATH) != 0) {
        fail("fs_init should succeed before fs_write/fs_read test");
    }
    for (i = 0; i < sizeof(file_write); ++i) {
        file_write[i] = (uint8_t)((i * 3u) & 0xFFu);
    }
    hw = fs_open("DATA.BIN");
    if (hw < 0) {
        fail("fs_open DATA.BIN for write failed");
    }
    if (fs_write(hw, file_write, (int)sizeof(file_write)) != (int)sizeof(file_write)) {
        fail("fs_write should write full payload");
    }
    fs_close(hw);

    hr = fs_open("DATA.BIN");
    if (hr < 0) {
        fail("fs_open DATA.BIN for read failed");
    }
    memset(file_read, 0, sizeof(file_read));
    if (fs_read(hr, file_read, (int)sizeof(file_read)) != (int)sizeof(file_read)) {
        fail("fs_read should read full payload");
    }
    if (memcmp(file_write, file_read, sizeof(file_write)) != 0) {
        fail("fs_read payload mismatch");
    }
    if (fs_read(hr, file_read, 68) != 68) {
        fail("fs_read should expose padded tail up to sector boundary");
    }
    if (fs_read(hr, file_read, 1) != 0) {
        fail("fs_read should report EOF once extent is exhausted");
    }
    fs_close(hr);

    if (fs_delete("DATA.BIN") != 0) {
        fail("fs_delete should succeed for existing file");
    }
    if (fs_exists("DATA.BIN") != 0) {
        fail("fs_delete should remove file from existence checks");
    }
    if (fs_open("DATA.BIN") != -1) {
        fail("deleted file should not be openable");
    }
    if (fs_delete("DATA.BIN") == 0) {
        fail("fs_delete should fail for already deleted file");
    }

    (void)TOTAL_BYTES;
    puts("PASS: test_fs_sector");
    return 0;
}
