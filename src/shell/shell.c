#include "shell.h"

#include "../compiler/compiler.h"
#include "../emu/mem.h"
#include "../fs/fs.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static void build_com_path(const char *src, char *out, size_t out_sz) {
    size_t i = 0u;
    size_t dot = (size_t)-1;
    if (out_sz == 0u) {
        return;
    }
    while (src[i] != '\0' && i < out_sz - 1u) {
        out[i] = src[i];
        if (src[i] == '.') {
            dot = i;
        }
        i++;
    }
    out[i] = '\0';
    if (dot != (size_t)-1 && dot + 4u < out_sz) {
        out[dot] = '.';
        out[dot + 1u] = 'c';
        out[dot + 2u] = 'o';
        out[dot + 3u] = 'm';
        out[dot + 4u] = '\0';
        return;
    }
    if (i + 4u < out_sz) {
        out[i++] = '.';
        out[i++] = 'c';
        out[i++] = 'o';
        out[i++] = 'm';
        out[i] = '\0';
    }
}

void shell_init(shell_t *sh) {
    if (sh == NULL) {
        return;
    }
    sh->last_cmd = SHELL_CMD_NONE;
    sh->arg[0] = '\0';
    sh->run_requested = 0;
    sh->run_entry = 0x0100u;
    sh->halt_requested = 0;
}

void shell_prompt(FILE *out) {
    if (out == NULL) {
        return;
    }
    (void)fputs("A> ", out);
}

shell_cmd_t shell_parse_line(shell_t *sh, const char *line) {
    char word[129];
    size_t wi = 0u;
    size_t i = 0u;

    if (sh == NULL || line == NULL) {
        return SHELL_CMD_UNKNOWN;
    }

    sh->arg[0] = '\0';
    while (line[i] != '\0' && isspace((unsigned char)line[i])) {
        i++;
    }
    while (line[i] != '\0' && !isspace((unsigned char)line[i]) && wi < sizeof(word) - 1u) {
        word[wi++] = (char)tolower((unsigned char)line[i]);
        i++;
    }
    word[wi] = '\0';

    while (line[i] != '\0' && isspace((unsigned char)line[i])) {
        i++;
    }
    if (line[i] != '\0') {
        size_t ai = 0u;
        while (line[i] != '\0' && ai < sizeof(sh->arg) - 1u) {
            sh->arg[ai++] = line[i++];
        }
        sh->arg[ai] = '\0';
    }

    if (word[0] == '\0') {
        sh->last_cmd = SHELL_CMD_NONE;
    } else if (streq(word, "dir")) {
        sh->last_cmd = SHELL_CMD_DIR;
    } else if (streq(word, "type")) {
        sh->last_cmd = SHELL_CMD_TYPE;
    } else if (streq(word, "run")) {
        sh->last_cmd = SHELL_CMD_RUN;
    } else if (streq(word, "cc")) {
        sh->last_cmd = SHELL_CMD_CC;
    } else if (streq(word, "del")) {
        sh->last_cmd = SHELL_CMD_DEL;
    } else if (streq(word, "cls")) {
        sh->last_cmd = SHELL_CMD_CLS;
    } else if (streq(word, "mem")) {
        sh->last_cmd = SHELL_CMD_MEM;
    } else if (streq(word, "halt")) {
        sh->last_cmd = SHELL_CMD_HALT;
    } else if (streq(word, "help")) {
        sh->last_cmd = SHELL_CMD_HELP;
    } else {
        sh->last_cmd = SHELL_CMD_UNKNOWN;
    }
    return sh->last_cmd;
}

void shell_render_result(shell_t *sh, FILE *out, shell_cmd_t cmd, const char *arg) {
    char names[64][13];
    uint8_t buf[256];
    int n;
    int i;
    int fh;
    int r;
    unsigned short load_addr = 0x0100u;
    char out_path[256];

    if (sh != NULL) {
        sh->run_requested = 0;
        sh->halt_requested = 0;
    }
    if (out == NULL) {
        return;
    }
    switch (cmd) {
        case SHELL_CMD_NONE:
            break;
        case SHELL_CMD_DIR:
            if (fs_init("disk.img") != 0) {
                (void)fputs("?\n", out);
                break;
            }
            n = fs_list(names, 64);
            if (n <= 0) {
                (void)fputs("(empty)\n", out);
                break;
            }
            for (i = 0; i < n && i < 64; ++i) {
                (void)fputs(names[i], out);
                (void)fputc('\n', out);
            }
            break;
        case SHELL_CMD_TYPE:
            if (arg == NULL || arg[0] == '\0') {
                (void)fputs("?\n", out);
                break;
            }
            if (fs_init("disk.img") != 0) {
                (void)fputs("?\n", out);
                break;
            }
            fh = fs_open(arg);
            if (fh < 0) {
                (void)fputs("?\n", out);
                break;
            }
            for (;;) {
                r = fs_read(fh, buf, (int)sizeof(buf));
                if (r <= 0) {
                    break;
                }
                (void)fwrite(buf, 1u, (size_t)r, out);
            }
            fs_close(fh);
            (void)fputc('\n', out);
            break;
        case SHELL_CMD_RUN:
            if (arg == NULL || arg[0] == '\0' || sh == NULL) {
                (void)fputs("?\n", out);
                break;
            }
            if (fs_init("disk.img") != 0) {
                (void)fputs("?\n", out);
                break;
            }
            fh = fs_open(arg);
            if (fh < 0) {
                (void)fputs("?\n", out);
                break;
            }
            while ((r = fs_read(fh, buf, (int)sizeof(buf))) > 0) {
                int bi;
                for (bi = 0; bi < r; ++bi) {
                    mem_write((addr_t)load_addr, buf[bi]);
                    load_addr++;
                    if (load_addr == 0u) {
                        fs_close(fh);
                        (void)fputs("?\n", out);
                        return;
                    }
                }
            }
            fs_close(fh);
            if (r < 0) {
                (void)fputs("?\n", out);
                break;
            }
            sh->run_requested = 1;
            sh->run_entry = 0x0100u;
            (void)fputs("RUN\n", out);
            break;
        case SHELL_CMD_HELP:
            (void)fputs("dir type run cc del cls mem halt help\n", out);
            break;
        case SHELL_CMD_CC:
            if (arg == NULL || arg[0] == '\0') {
                (void)fputs("?\n", out);
                break;
            }
            build_com_path(arg, out_path, sizeof(out_path));
            if (cc_compile(arg, out_path) != 0) {
                (void)fputs("?\n", out);
                break;
            }
            (void)fputs(out_path, out);
            (void)fputc('\n', out);
            break;
        case SHELL_CMD_DEL:
            if (arg == NULL || arg[0] == '\0') {
                (void)fputs("?\n", out);
                break;
            }
            if (fs_init("disk.img") != 0 || fs_delete(arg) != 0) {
                (void)fputs("?\n", out);
                break;
            }
            (void)fputs("OK\n", out);
            break;
        case SHELL_CMD_CLS:
            (void)fputs("\x1b[2J\x1b[H", out);
            break;
        case SHELL_CMD_MEM:
            (void)fputs("0000-00FF BIOS\n", out);
            (void)fputs("0100-3FFF TPA\n", out);
            (void)fputs("4000-7FFF KERNEL\n", out);
            (void)fputs("8000-BFFF SHELL\n", out);
            (void)fputs("C000-EFFF FS\n", out);
            (void)fputs("F000-FDFF STACK\n", out);
            (void)fputs("FE00-FEFF IO\n", out);
            (void)fputs("FF00-FFFF META\n", out);
            break;
        case SHELL_CMD_HALT:
            if (sh != NULL) {
                sh->halt_requested = 1;
            }
            (void)fputs("HALT\n", out);
            break;
        case SHELL_CMD_UNKNOWN:
            (void)fputs("?\n", out);
            break;
        default:
            (void)fputs("OK\n", out);
            break;
    }
}
