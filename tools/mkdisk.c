/* mkdisk — build and inspect TuringOS disk images through the real filesystem
 * (src/fs/fs.c) and the POSIX HAL (src/hal/hal_posix.c), linked from build/libtos.a.
 *
 *   mkdisk <image> [--format] [--add <hostfile>[:NAME.EXT]]... [--ls] [--extract NAME.EXT <out>]
 *
 * With no arguments: mkdisk build/disk/disk.img --format.
 * The image is created (blank, formatted) when it does not exist: fs_init sees
 * 0 bytes from hal_disk_load and formats a blank image. --format forces a blank
 * directory. Flags are applied in order, then the image is flushed back to the
 * host through fs_flush. --ls prints one NAME.EXT per line and nothing else on
 * stdout; errors go to stderr and exit with status 1.
 */
#include "tos.h"
#include "fs/fs.h"
#include "hal/hal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MKDISK_DEFAULT_IMAGE "build/disk/disk.img"
#define MKDISK_BUF_BYTES     65536u
#define MKDISK_FILE_MAX      32768u   /* one directory entry: 16 x 2 KB blocks (see src/fs/fs.c) */

static uint8_t g_buf[MKDISK_BUF_BYTES];

static void usage(FILE *f)
{
    fprintf(f, "usage: mkdisk <image> [--format] [--add <hostfile>[:NAME.EXT]]... [--ls] [--extract NAME.EXT <out>]\n");
}

static char up(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

/* Same character set the filesystem accepts in a name. */
static int name_char_ok(char c)
{
    unsigned char u = (unsigned char)c;
    if (u < 0x21u || u > 0x7Eu) {
        return 0;
    }
    return strchr(".*?/\\:;,=<>[]\"|", c) == NULL ? 1 : 0;
}

/* Default disk name for a host path: basename upper-cased, stem up to 8 chars,
 * extension (after the last dot) up to 3 chars; characters the fs rejects are
 * dropped. Returns -1 when no stem survives. */
static int derive_name(const char *path, char *out, size_t cap)
{
    const char *base;
    const char *dot;
    const char *p;
    size_t n = 0u;
    size_t ext = 0u;

    if (cap < 13u) {
        return -1;
    }
    base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;
    dot = strrchr(base, '.');
    if (dot == base) {
        dot = NULL;                     /* ".hidden" has no extension */
    }
    for (p = base; *p != '\0' && p != dot && n < 8u; ++p) {
        if (name_char_ok(*p)) {
            out[n++] = up(*p);
        }
    }
    if (n == 0u) {
        return -1;
    }
    if (dot != NULL) {
        size_t mark = n;
        out[n++] = '.';
        for (p = dot + 1; *p != '\0' && ext < 3u; ++p) {
            if (name_char_ok(*p)) {
                out[n++] = up(*p);
                ext++;
            }
        }
        if (ext == 0u) {
            n = mark;                   /* drop a dangling '.' */
        }
    }
    out[n] = '\0';
    return 0;
}

/* Reads a host file into g_buf. Returns its length, or -1 (message printed). */
static long read_host_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    size_t got;
    int extra;

    if (fp == NULL) {
        fprintf(stderr, "mkdisk: cannot open %s\n", path);
        return -1;
    }
    got = fread(g_buf, 1u, sizeof(g_buf), fp);
    extra = fgetc(fp);
    fclose(fp);
    if (extra != EOF || got > MKDISK_FILE_MAX) {
        fprintf(stderr, "mkdisk: %s is larger than %u bytes\n", path, (unsigned)MKDISK_FILE_MAX);
        return -1;
    }
    return (long)got;
}

static int do_add(const char *arg)
{
    char path[1024];
    char name[64];
    const char *colon;
    long len;

    /* "<hostfile>[:NAME.EXT]": split at the last ':' whose tail contains no '/'. */
    colon = strrchr(arg, ':');
    if (colon != NULL && colon != arg && strchr(colon, '/') == NULL) {
        size_t plen = (size_t)(colon - arg);
        if (plen >= sizeof(path)) {
            fprintf(stderr, "mkdisk: path too long: %s\n", arg);
            return -1;
        }
        memcpy(path, arg, plen);
        path[plen] = '\0';
        if (strlen(colon + 1) >= sizeof(name)) {
            fprintf(stderr, "mkdisk: name too long: %s\n", colon + 1);
            return -1;
        }
        strcpy(name, colon + 1);
    } else {
        if (strlen(arg) >= sizeof(path)) {
            fprintf(stderr, "mkdisk: path too long: %s\n", arg);
            return -1;
        }
        strcpy(path, arg);
        if (derive_name(path, name, sizeof(name)) != 0) {
            fprintf(stderr, "mkdisk: cannot derive a disk name from %s\n", path);
            return -1;
        }
    }

    len = read_host_file(path);
    if (len < 0) {
        return -1;
    }
    if (fs_put_file(name, g_buf, (uint32_t)len) != 0) {
        fprintf(stderr, "mkdisk: cannot add %s as %s (bad name, file too large, or disk full)\n", path, name);
        return -1;
    }
    return 0;
}

static int do_ls(void)
{
    static char names[TOS_DISK_DIR_ENTRIES][13];
    int n;
    int i;

    n = fs_list(names, (int)TOS_DISK_DIR_ENTRIES);
    if (n < 0) {
        fprintf(stderr, "mkdisk: cannot list directory\n");
        return -1;
    }
    for (i = 0; i < n; ++i) {
        printf("%s\n", names[i]);
    }
    fflush(stdout);
    return 0;
}

static int do_extract(const char *disk_name, const char *out_path)
{
    FILE *fp;
    int len;

    if (fs_file_size(disk_name) < 0) {
        fprintf(stderr, "mkdisk: no such file on disk: %s\n", disk_name);
        return -1;
    }
    len = fs_get_file(disk_name, g_buf, (uint32_t)sizeof(g_buf));
    if (len < 0) {
        fprintf(stderr, "mkdisk: cannot read %s\n", disk_name);
        return -1;
    }
    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "mkdisk: cannot create %s\n", out_path);
        return -1;
    }
    if (len > 0 && fwrite(g_buf, 1u, (size_t)len, fp) != (size_t)len) {
        fprintf(stderr, "mkdisk: short write to %s\n", out_path);
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) {
        fprintf(stderr, "mkdisk: cannot close %s\n", out_path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *image = MKDISK_DEFAULT_IMAGE;
    int argi = 1;
    int format_first = 0;
    int rc = 0;
    FILE *chk;

    if (argc < 2) {
        format_first = 1;               /* mkdisk == mkdisk build/disk/disk.img --format */
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    } else if (strncmp(argv[1], "--", 2) != 0) {
        image = argv[1];
        argi = 2;
    }

    /* No hal_init/hal_shutdown: mkdisk never touches the console or the terminal,
     * only the disk image path registered through the "disk_a" option. */
    if (hal_set_option("disk_a", image) != 0) {
        fprintf(stderr, "mkdisk: the HAL rejected the disk image path %s\n", image);
        return 1;
    }
    (void)fs_init(1u);                  /* missing image -> blank formatted image */
    if (format_first) {
        (void)fs_format(0u);
    }

    for (; argi < argc && rc == 0; ++argi) {
        const char *a = argv[argi];
        if (strcmp(a, "--format") == 0) {
            if (fs_format(0u) != 0) {
                fprintf(stderr, "mkdisk: format failed\n");
                rc = 1;
            }
        } else if (strcmp(a, "--add") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "mkdisk: --add needs <hostfile>[:NAME.EXT]\n");
                rc = 1;
            } else {
                argi++;
                if (do_add(argv[argi]) != 0) {
                    rc = 1;
                }
            }
        } else if (strcmp(a, "--ls") == 0) {
            if (do_ls() != 0) {
                rc = 1;
            }
        } else if (strcmp(a, "--extract") == 0) {
            if (argi + 2 >= argc) {
                fprintf(stderr, "mkdisk: --extract needs NAME.EXT <out>\n");
                rc = 1;
            } else {
                if (do_extract(argv[argi + 1], argv[argi + 2]) != 0) {
                    rc = 1;
                }
                argi += 2;
            }
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
        } else {
            fprintf(stderr, "mkdisk: unknown argument %s\n", a);
            usage(stderr);
            rc = 1;
        }
    }

    if (rc != 0) {
        return rc;
    }

    fs_flush();
    chk = fopen(image, "rb");
    if (chk == NULL) {
        fprintf(stderr, "mkdisk: could not write %s (does its directory exist?)\n", image);
        return 1;
    }
    fclose(chk);
    return 0;
}
