/* TuringOS v2 — native (POSIX) host abstraction layer.
 *
 * The only core file allowed to include <stdio.h>. Everything the machine sees of the
 * outside world (console bytes, keys, time, disk images, the shell blob) comes through here.
 *
 * Console input sources, in priority order:
 *   1. the hal_con_push queue (256 bytes) — always available (the API layer feeds it)
 *   2. the "stdin_script" file, then sticky EOF — always available once the option is set
 *   3. the process's stdin — only after hal_init() (the CLI calls it; an embedder that only
 *      pushes bytes through the API never sees stdin, so CONIN parks instead of hitting EOF):
 *        - a TTY is polled without blocking (raw mode when option raw != "0");
 *        - a pipe/file is read with a blocking read inside hal_con_in_ready() so scripted
 *          sessions never spin, and EOF is reported as "ready" (hal_con_in() then returns -2).
 *
 * Console output is written to stdout AND kept in a 64 KB ring readable through
 * hal_con_out_pending()/hal_con_out_pop() (extra, non-hal.h helpers used by the API layer). */
#define _POSIX_C_SOURCE 200809L

#include "hal.h"
#include "../tos.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define HAL_PATH_CAP     1024u
#define HAL_PUSH_CAP     256u
#define HAL_TTY_CAP      1024u
#define HAL_OUT_CAP      65536u
#define HAL_SCRIPT_CHUNK 4096u
#define HAL_SHELL_CAP    16384u
#define HAL_KEY_HOLD_MS  150u
#define HAL_TTY_WAIT_MS  1
#define HAL_FRAME_CAP    4096u

/* ---- options ----------------------------------------------------------- */
static char g_disk_path[2][HAL_PATH_CAP];
static char g_shell_path[HAL_PATH_CAP];
static char g_snap_dir[HAL_PATH_CAP];
static char g_script_path[HAL_PATH_CAP];
static int  g_fps = 60;
static int  g_raw_opt = -1;       /* -1 auto (TTY => raw), 0 off, 1 on */
static int  g_display_opt = 0;

/* ---- terminal ---------------------------------------------------------- */
static int            g_inited = 0;
static int            g_tty = 0;
static int            g_raw_active = 0;
static struct termios g_saved_termios;
static int            g_display_started = 0;

/* ---- console input ----------------------------------------------------- */
static uint8_t  g_push[HAL_PUSH_CAP];
static uint32_t g_push_head = 0u, g_push_tail = 0u, g_push_count = 0u;
static uint8_t  g_ttyq[HAL_TTY_CAP];
static uint32_t g_ttyq_head = 0u, g_ttyq_tail = 0u, g_ttyq_count = 0u;
static int      g_peek = -1;          /* one byte read ahead from a pipe */
static int      g_stdin_eof = 0;
static int      g_esc_state = 0;      /* 0 normal, 1 saw ESC, 2 saw ESC [ */

static int      g_script_fd = -1;
static int      g_script_opened = 0;
static int      g_script_eof = 0;
static uint8_t  g_script_buf[HAL_SCRIPT_CHUNK];
static uint32_t g_script_pos = 0u, g_script_len = 0u;

/* ---- console output ---------------------------------------------------- */
static uint8_t  g_out[HAL_OUT_CAP];
static uint32_t g_out_head = 0u, g_out_tail = 0u, g_out_count = 0u;

/* ---- keys -------------------------------------------------------------- */
static uint32_t g_key_until[8];       /* per TOS_KEY_* bit: hal_time_ms() until which it is held */
static uint8_t  g_host_keys = 0u;

/* ---- time -------------------------------------------------------------- */
static uint64_t g_t0_ns = 0u;
static int      g_t0_set = 0;
static uint64_t g_next_frame_ns = 0u;

/* ---- shell override ---------------------------------------------------- */
static uint8_t  g_shell_buf[HAL_SHELL_CAP];
static uint32_t g_shell_len = 0u;
static int      g_shell_loaded = 0;
static int      g_shell_failed = 0;

/* ======================================================================== */
/* time                                                                     */
/* ======================================================================== */

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint32_t hal_time_ms(void) {
    uint64_t n = now_ns();
    if (!g_t0_set) {
        g_t0_ns = n;
        g_t0_set = 1;
    }
    return (uint32_t)((n - g_t0_ns) / 1000000ull);
}

static void sleep_ns(uint64_t ns) {
    struct timespec req;
    struct timespec rem;
    req.tv_sec = (time_t)(ns / 1000000000ull);
    req.tv_nsec = (long)(ns % 1000000000ull);
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            break;
        }
        req = rem;
    }
}

/* ======================================================================== */
/* raw output                                                               */
/* ======================================================================== */

static void write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0u) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void write_str(const char *s) {
    write_all(STDOUT_FILENO, s, strlen(s));
}

/* ======================================================================== */
/* terminal mode                                                            */
/* ======================================================================== */

static void restore_terminal(void) {
    if (g_raw_active) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_raw_active = 0;
    }
}

static void on_signal(int sig) {
    restore_terminal();
    if (g_display_started) {
        write_str("\x1b[?25h");
    }
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

static void enter_raw(void) {
    struct termios t;
    if (g_raw_active) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
        return;                     /* not a terminal: --raw=1 on a pipe is a no-op */
    }
    t = g_saved_termios;
    t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN);   /* ISIG stays: Ctrl-C still works */
    t.c_iflag &= (tcflag_t)~(IXON);                     /* ICRNL stays: Enter arrives as '\n' */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) {
        return;
    }
    g_raw_active = 1;
}

static void apply_raw_option(void) {
    int want = (g_raw_opt < 0) ? g_tty : g_raw_opt;
    if (want) {
        enter_raw();
    } else {
        restore_terminal();
    }
}

static void script_close(void) {
    if (g_script_fd >= 0) {
        (void)close(g_script_fd);
    }
    g_script_fd = -1;
    g_script_opened = 0;
    g_script_eof = 0;
    g_script_pos = 0u;
    g_script_len = 0u;
}

void hal_init(void) {
    int b;
    (void)hal_time_ms();
    g_tty = isatty(STDIN_FILENO) ? 1 : 0;
    apply_raw_option();
    if (!g_inited) {
        (void)atexit(hal_shutdown);
        (void)signal(SIGINT, on_signal);
        (void)signal(SIGTERM, on_signal);
        (void)signal(SIGHUP, on_signal);
        (void)signal(SIGSEGV, on_signal);   /* crash paths restore the terminal too, then re-raise */
        (void)signal(SIGBUS, on_signal);
        (void)signal(SIGABRT, on_signal);
        (void)signal(SIGFPE, on_signal);
    }
    g_inited = 1;

    g_push_head = g_push_tail = g_push_count = 0u;
    g_ttyq_head = g_ttyq_tail = g_ttyq_count = 0u;
    g_esc_state = 0;
    g_out_head = g_out_tail = g_out_count = 0u;
    for (b = 0; b < 8; b++) {
        g_key_until[b] = 0u;
    }
    g_host_keys = 0u;
    script_close();                 /* a fresh machine replays the script from its start */
    g_next_frame_ns = 0u;
}

void hal_shutdown(void) {
    restore_terminal();
    if (g_display_started) {
        write_str("\x1b[?25h");
        g_display_started = 0;
    }
    script_close();
}

/* ======================================================================== */
/* options                                                                  */
/* ======================================================================== */

static void copy_opt(char *dst, const char *src) {
    size_t n = strlen(src);
    if (n >= HAL_PATH_CAP) {
        n = HAL_PATH_CAP - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int hal_set_option(const char *key, const char *value) {
    if (key == NULL) {
        return -1;
    }
    if (value == NULL) {
        value = "";
    }
    if (strcmp(key, "disk_a") == 0) {
        copy_opt(g_disk_path[0], value);
        return 0;
    }
    if (strcmp(key, "disk_b") == 0) {
        copy_opt(g_disk_path[1], value);
        return 0;
    }
    if (strcmp(key, "shell") == 0) {
        copy_opt(g_shell_path, value);
        g_shell_loaded = 0;
        g_shell_failed = 0;
        return 0;
    }
    if (strcmp(key, "snap_dir") == 0) {
        copy_opt(g_snap_dir, value);
        return 0;
    }
    if (strcmp(key, "fps") == 0) {
        g_fps = atoi(value);
        if (g_fps < 0) {
            g_fps = 0;
        }
        g_next_frame_ns = 0u;
        return 0;
    }
    if (strcmp(key, "raw") == 0) {
        if (strcmp(value, "0") == 0) {
            g_raw_opt = 0;
        } else if (strcmp(value, "1") == 0) {
            g_raw_opt = 1;
        } else {
            g_raw_opt = -1;
        }
        if (g_inited) {
            apply_raw_option();
        }
        return 0;
    }
    if (strcmp(key, "display") == 0) {
        g_display_opt = (atoi(value) != 0) ? 1 : 0;
        return 0;
    }
    if (strcmp(key, "stdin_script") == 0) {
        copy_opt(g_script_path, value);
        script_close();
        return 0;
    }
    return -1;
}

/* ======================================================================== */
/* console output                                                           */
/* ======================================================================== */

void hal_con_out(uint8_t ch) {
    write_all(STDOUT_FILENO, &ch, 1u);
    if (g_out_count >= HAL_OUT_CAP) {
        g_out_tail = (g_out_tail + 1u) % HAL_OUT_CAP;   /* drop the oldest */
        g_out_count--;
    }
    g_out[g_out_head] = ch;
    g_out_head = (g_out_head + 1u) % HAL_OUT_CAP;
    g_out_count++;
}

/* Extra helpers (not in hal.h): the API layer's tos_con_pop/tos_con_pending read these. */
int hal_con_out_pending(void) {
    return (int)g_out_count;
}

int hal_con_out_pop(void) {
    int ch;
    if (g_out_count == 0u) {
        return -1;
    }
    ch = g_out[g_out_tail];
    g_out_tail = (g_out_tail + 1u) % HAL_OUT_CAP;
    g_out_count--;
    return ch;
}

/* ======================================================================== */
/* keys + TTY pump                                                          */
/* ======================================================================== */

static void key_hit(int bit) {
    uint32_t now = hal_time_ms();
    uint32_t until = now + HAL_KEY_HOLD_MS;
    if (until == 0u) {
        until = 1u;
    }
    if (bit >= 0 && bit < 8) {
        g_key_until[bit] = until;
    }
    g_key_until[7] = until;         /* TOS_KEY_ANY */
}

static void ttyq_push(uint8_t ch) {
    if (g_ttyq_count >= HAL_TTY_CAP) {
        return;
    }
    g_ttyq[g_ttyq_head] = ch;
    g_ttyq_head = (g_ttyq_head + 1u) % HAL_TTY_CAP;
    g_ttyq_count++;
}

static int ttyq_pop(void) {
    int ch;
    if (g_ttyq_count == 0u) {
        return -1;
    }
    ch = g_ttyq[g_ttyq_tail];
    g_ttyq_tail = (g_ttyq_tail + 1u) % HAL_TTY_CAP;
    g_ttyq_count--;
    return ch;
}

/* Local echo: raw mode turned the terminal's own echo off. Skipped while the framebuffer
 * is being drawn so game keys do not scribble over the picture. */
static void echo_byte(uint8_t ch) {
    if (!g_raw_active || g_display_opt) {
        return;
    }
    if (ch == 8u || ch == 127u) {
        write_str("\b \b");
    } else if (ch == (uint8_t)'\n' || ch == (uint8_t)'\r') {
        write_str("\n");
    } else if (ch >= 0x20u && ch < 0x7Fu) {
        write_all(STDOUT_FILENO, &ch, 1u);
    }
}

/* A plain byte from the terminal: set its key bit and queue it as console text. */
static void classify_byte(uint8_t ch) {
    switch (ch) {
        case 'w':
        case 'W':
            key_hit(0);
            break;
        case 's':
        case 'S':
            key_hit(1);
            break;
        case ' ':
            key_hit(4);
            break;
        case '\n':
        case '\r':
            key_hit(6);
            break;
        default:
            key_hit(7);
            break;
    }
    ttyq_push(ch);
    echo_byte(ch);
}

/* A bare ESC keypress: key bit only, never console text. */
static void bare_escape(void) {
    key_hit(5);
}

static void pump_byte(uint8_t ch) {
    if (g_esc_state == 1) {
        if (ch == (uint8_t)'[') {
            g_esc_state = 2;
            return;
        }
        g_esc_state = 0;
        bare_escape();              /* it was a lone ESC; fall into normal handling of ch */
    } else if (g_esc_state == 2) {
        if (ch == (uint8_t)'A') {
            key_hit(2);             /* up */
            g_esc_state = 0;
        } else if (ch == (uint8_t)'B') {
            key_hit(3);             /* down */
            g_esc_state = 0;
        } else if (ch >= 0x40u && ch <= 0x7Eu) {
            g_esc_state = 0;        /* other final byte: sequence consumed */
            key_hit(7);
        }
        /* parameter / intermediate bytes: stay in state 2 */
        return;
    }
    if (ch == 0x1Bu) {
        g_esc_state = 1;
        return;
    }
    if (g_raw_active && ch == 0x04u) {
        g_stdin_eof = 1;            /* Ctrl-D in raw mode = console EOF */
        return;
    }
    classify_byte(ch);
}

/* Non-blocking: move whatever the TTY has into the tty queue / key state. */
static void tty_pump(void) {
    uint8_t buf[64];
    struct pollfd pfd;
    int pr;
    ssize_t n;
    ssize_t i;

    if (!g_tty || g_stdin_eof) {
        return;
    }
    for (;;) {
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        pr = poll(&pfd, 1, 0);
        if (pr <= 0) {
            break;
        }
        if ((pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            break;
        }
        n = read(STDIN_FILENO, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            if (!g_raw_active) {
                g_stdin_eof = 1;    /* cooked mode: Ctrl-D at line start */
            }
            break;
        }
        for (i = 0; i < n; i++) {
            pump_byte(buf[i]);
        }
        if (n < (ssize_t)sizeof buf) {
            break;
        }
    }
    if (g_esc_state == 1) {
        g_esc_state = 0;            /* lone ESC with nothing behind it */
        bare_escape();
    }
}

static void tty_wait_ms(int ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    (void)poll(&pfd, 1, ms);
}

uint8_t hal_keys(void) {
    uint8_t mask = g_host_keys;
    uint32_t now;
    int b;
    if (g_inited && g_tty) {
        tty_pump();
    }
    now = hal_time_ms();
    for (b = 0; b < 8; b++) {
        if (g_key_until[b] != 0u && (int32_t)(g_key_until[b] - now) > 0) {
            mask |= (uint8_t)(1u << b);
        }
    }
    return mask;
}

void hal_keys_set(uint8_t mask) {
    g_host_keys = mask;
}

/* ======================================================================== */
/* console input                                                            */
/* ======================================================================== */

/* Extra helper (not in hal.h, like hal_con_out_pending): pushed bytes the machine has not read
 * yet. The API layer uses it so a seek pulls back only its own input, never the stdin script. */
uint32_t hal_con_push_pending(void) {
    return g_push_count;
}

int hal_con_push(uint8_t ch) {
    if (g_push_count >= HAL_PUSH_CAP) {
        return -1;
    }
    g_push[g_push_head] = ch;
    g_push_head = (g_push_head + 1u) % HAL_PUSH_CAP;
    g_push_count++;
    return 0;
}

static int push_pop(void) {
    int ch;
    if (g_push_count == 0u) {
        return -1;
    }
    ch = g_push[g_push_tail];
    g_push_tail = (g_push_tail + 1u) % HAL_PUSH_CAP;
    g_push_count--;
    return ch;
}

static int script_active(void) {
    return g_script_path[0] != '\0';
}

/* 1 when a script byte is buffered (filling from the file as needed), 0 at script EOF. */
static int script_fill(void) {
    if (g_script_eof) {
        return 0;
    }
    if (!g_script_opened) {
        g_script_opened = 1;
        g_script_fd = open(g_script_path, O_RDONLY);
        if (g_script_fd < 0) {
            g_script_eof = 1;       /* missing file == empty script */
            return 0;
        }
    }
    if (g_script_pos < g_script_len) {
        return 1;
    }
    for (;;) {
        ssize_t n = read(g_script_fd, g_script_buf, sizeof g_script_buf);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            (void)close(g_script_fd);
            g_script_fd = -1;
            g_script_eof = 1;
            return 0;
        }
        g_script_pos = 0u;
        g_script_len = (uint32_t)n;
        return 1;
    }
}

/* Blocking read of one byte from a pipe/file stdin into the peek slot; sets EOF. */
static void pipe_fill_peek(void) {
    uint8_t b;
    if (g_peek >= 0 || g_stdin_eof) {
        return;
    }
    for (;;) {
        ssize_t n = read(STDIN_FILENO, &b, 1u);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            g_stdin_eof = 1;
            return;
        }
        g_peek = b;
        return;
    }
}

int hal_con_in_ready(void) {
    if (g_push_count > 0u) {
        return 1;
    }
    if (script_active()) {
        return 1;                   /* a byte or the script's EOF */
    }
    if (!g_inited) {
        return 0;                   /* embedded use: the push queue is the whole console */
    }
    if (g_peek >= 0 || g_stdin_eof) {
        return 1;
    }
    if (g_tty) {
        tty_pump();
        if (g_ttyq_count > 0u || g_stdin_eof) {
            return 1;
        }
        tty_wait_ms(HAL_TTY_WAIT_MS);   /* keeps an idle prompt from spinning a core */
        tty_pump();
        return (g_ttyq_count > 0u || g_stdin_eof) ? 1 : 0;
    }
    pipe_fill_peek();               /* blocks until a byte or EOF: piped scripts never spin */
    return 1;
}

int hal_con_in(void) {
    int ch = push_pop();
    if (ch >= 0) {
        return ch;
    }
    if (script_active()) {
        if (script_fill()) {
            return g_script_buf[g_script_pos++];
        }
        return -2;
    }
    if (!g_inited) {
        return -1;
    }
    if (g_peek >= 0) {
        ch = g_peek;
        g_peek = -1;
        return ch;
    }
    if (g_stdin_eof) {
        return -2;
    }
    if (g_tty) {
        tty_pump();
        ch = ttyq_pop();
        if (ch >= 0) {
            return ch;
        }
        return g_stdin_eof ? -2 : -1;
    }
    pipe_fill_peek();
    if (g_peek >= 0) {
        ch = g_peek;
        g_peek = -1;
        return ch;
    }
    return -2;
}

/* ======================================================================== */
/* frames / display                                                         */
/* ======================================================================== */

void hal_vsync(void) {
    uint64_t period;
    uint64_t now;
    if (g_fps <= 0 || !g_display_opt) {
        return;                     /* nobody is watching the frames: run flat out */
    }
    period = 1000000000ull / (uint64_t)g_fps;
    now = now_ns();
    if (g_next_frame_ns == 0u || now > g_next_frame_ns + period) {
        g_next_frame_ns = now + period;   /* first frame or fell behind: resync */
        return;
    }
    if (now < g_next_frame_ns) {
        sleep_ns(g_next_frame_ns - now);
    }
    g_next_frame_ns += period;
}

void hal_sleep_ms(uint32_t ms) {
    if (ms == 0u) {
        return;
    }
    sleep_ns((uint64_t)ms * 1000000ull);
}

void hal_display(const uint8_t *fb) {
    static uint8_t buf[HAL_FRAME_CAP];
    uint32_t n = 0u;
    uint32_t r;
    uint32_t x;
    if (!g_display_opt || fb == NULL) {
        return;
    }
    if (!g_display_started) {
        write_str("\x1b[2J\x1b[?25l");   /* clear once, hide the cursor */
        g_display_started = 1;
    }
    buf[n++] = 0x1Bu;                   /* ESC [ H : cursor home, then overdraw in place */
    buf[n++] = (uint8_t)'[';
    buf[n++] = (uint8_t)'H';
    for (r = 0u; r < TOS_DISPLAY_H / 2u; r++) {
        for (x = 0u; x < TOS_DISPLAY_W; x++) {
            int top = (fb[(2u * r) * 8u + (x >> 3)] >> (7u - (x & 7u))) & 1;
            int bot = (fb[(2u * r + 1u) * 8u + (x >> 3)] >> (7u - (x & 7u))) & 1;
            if (top && bot) {
                buf[n++] = 0xE2u; buf[n++] = 0x96u; buf[n++] = 0x88u;   /* U+2588 full block */
            } else if (top) {
                buf[n++] = 0xE2u; buf[n++] = 0x96u; buf[n++] = 0x80u;   /* U+2580 upper half */
            } else if (bot) {
                buf[n++] = 0xE2u; buf[n++] = 0x96u; buf[n++] = 0x84u;   /* U+2584 lower half */
            } else {
                buf[n++] = (uint8_t)' ';
            }
        }
        buf[n++] = (uint8_t)'\n';
    }
    write_all(STDOUT_FILENO, buf, n);
}

/* ======================================================================== */
/* snapshots                                                                */
/* ======================================================================== */

static void write_file(const char *path, const uint8_t *data, uint32_t len) {
    FILE *f = fopen(path, "wb");
    size_t w;
    if (f == NULL) {
        return;
    }
    w = fwrite(data, 1u, (size_t)len, f);
    (void)w;
    (void)fclose(f);
}

void hal_snapshot(const uint8_t *tape, uint32_t tape_len, const uint8_t *meta, uint32_t meta_len) {
    char path[HAL_PATH_CAP + 16u];
    if (g_snap_dir[0] == '\0') {
        return;
    }
    (void)mkdir(g_snap_dir, 0755);
    if (tape != NULL && tape_len > 0u) {
        (void)snprintf(path, sizeof path, "%s/tape.bin", g_snap_dir);
        write_file(path, tape, tape_len);
    }
    if (meta != NULL && meta_len > 0u) {
        (void)snprintf(path, sizeof path, "%s/meta.bin", g_snap_dir);
        write_file(path, meta, meta_len);
    }
}

/* ======================================================================== */
/* disk images                                                              */
/* ======================================================================== */

int hal_disk_load(uint8_t disk, uint8_t *buf, uint32_t cap) {
    FILE *f;
    size_t n;
    if (disk >= 2u || buf == NULL || cap == 0u || g_disk_path[disk][0] == '\0') {
        return 0;
    }
    f = fopen(g_disk_path[disk], "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1u, (size_t)cap, f);
    (void)fclose(f);
    return (int)n;
}

int hal_disk_save(uint8_t disk, const uint8_t *buf, uint32_t len) {
    FILE *f;
    size_t w;
    int rc;
    if (disk >= 2u || buf == NULL || g_disk_path[disk][0] == '\0') {
        return -1;
    }
    f = fopen(g_disk_path[disk], "wb");
    if (f == NULL) {
        return -1;
    }
    w = fwrite(buf, 1u, (size_t)len, f);
    rc = fclose(f);
    return (w == (size_t)len && rc == 0) ? 0 : -1;
}

/* ======================================================================== */
/* shell blob                                                               */
/* ======================================================================== */

int hal_shell_blob(const uint8_t **ptr, uint32_t *len) {
    if (ptr == NULL || len == NULL) {
        return -1;
    }
    if (g_shell_path[0] != '\0') {
        if (!g_shell_loaded) {
            FILE *f = fopen(g_shell_path, "rb");
            g_shell_loaded = 1;
            g_shell_failed = 1;
            g_shell_len = 0u;
            if (f != NULL) {
                size_t n = fread(g_shell_buf, 1u, sizeof g_shell_buf, f);
                (void)fclose(f);
                if (n > 0u && n <= (size_t)TOS_TPA_SIZE) {
                    g_shell_len = (uint32_t)n;
                    g_shell_failed = 0;
                }
            }
        }
        if (g_shell_failed) {
            return -1;
        }
        *ptr = g_shell_buf;
        *len = g_shell_len;
        return 0;
    }
    if (tos_shell_blob_len == 0u) {
        return -1;
    }
    *ptr = tos_shell_blob;
    *len = tos_shell_blob_len;
    return 0;
}
