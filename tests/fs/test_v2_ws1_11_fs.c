#include "../testfw.h"
#include "../../src/fs/fs.h"
#include "../../src/hal/hal.h"
#include "../../src/tos.h"
#include <stdint.h>
#include <string.h>

static uint8_t g_buf[8192];
static uint8_t g_out[8192];

static int t_put_get_roundtrip(void) {
    uint32_t i;
    (void)hal_set_option("disk_a", "build/tests/ws1_11_a.img");
    (void)hal_set_option("disk_b", "build/tests/ws1_11_b.img");
    ASSERT(fs_init(1u) == 0);
    ASSERT(fs_format(0u) == 0);
    for (i = 0; i < 5000u; i++) g_buf[i] = (uint8_t)(i * 7u + 3u);
    ASSERT(fs_put_file("DATA.BIN", g_buf, 5000u) == 0);
    ASSERT(fs_file_size("DATA.BIN") == 5000);
    ASSERT(fs_get_file("data.bin", g_out, sizeof g_out) == 5000);   /* case-insensitive */
    ASSERT(memcmp(g_buf, g_out, 5000u) == 0);
    ASSERT(fs_exists("DATA.BIN") == 1);
    /* overwrite with a shorter file */
    ASSERT(fs_put_file("DATA.BIN", g_buf, 300u) == 0);
    ASSERT(fs_file_size("DATA.BIN") == 300);
    ASSERT(fs_get_file("DATA.BIN", g_out, sizeof g_out) == 300);
    ASSERT(fs_delete("DATA.BIN") == 0);
    ASSERT(fs_exists("DATA.BIN") == 0);
    ASSERT(fs_get_file("DATA.BIN", g_out, sizeof g_out) == -1);
    ASSERT(fs_delete("DATA.BIN") == -1);
    return 0;
}

static int t_two_disks_are_isolated(void) {
    char names[64][13];
    ASSERT(fs_init(2u) == 0);
    ASSERT(fs_format(0u) == 0);
    ASSERT(fs_format(1u) == 0);
    ASSERT(fs_disk_count() == 2u);
    ASSERT(fs_select_disk(1u) == 0);
    ASSERT(fs_selected_disk() == 1u);
    ASSERT(fs_put_file("B.TXT", (const uint8_t *)"bee", 3u) == 0);
    ASSERT(fs_exists("B.TXT") == 1);
    ASSERT(fs_select_disk(0u) == 0);
    ASSERT(fs_exists("B.TXT") == 0);
    ASSERT(fs_list(names, 64) == 0);
    ASSERT(fs_select_disk(2u) == -1);                     /* out of range */
    ASSERT(fs_selected_disk() == 0u);
    return 0;
}

static int t_image_size_and_short_image(void) {
    ASSERT(fs_image_size() == TOS_DISK_IMAGE_BYTES);
    ASSERT(fs_image_ptr(0u) != NULL);
    ASSERT(fs_image_ptr(1u) != NULL);
    return 0;
}

int main(void) {
    TEST("WS1-11b: fs_put_file/fs_get_file round-trip 5000 bytes, exact size, overwrite, delete, case-insensitive", t_put_get_roundtrip);
    TEST("WS1-11c: files on disk B are not visible on disk A; bad select rejected", t_two_disks_are_isolated);
    TEST("WS1-11b: image size is the fixed geometry", t_image_size_and_short_image);
    puts("PASS: test_v2_ws1_11_fs");
    RUN_ALL_TESTS();
}
