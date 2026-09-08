/* TuringOS v2 — native command-line entry point.
 * turingos [--tapes=N] [--len=N] [--hz=N] [--seed=N] [--disks=N] [--disk=<a.img>] [--disk-b=<b.img>] [--snap=N] [--input=console|keys]
 *          [--shell=<shell.com>] [--snap-dir=<dir>] [--fps=N] [--display] [--raw=0|1]
 *          [--stdin-script=<file>] [--trace] [--version]
 * Every byte of output goes through the HAL (no stdio here). The exit line is
 * "TuringOS halted (reason=<NAME>) after <N> steps\n" and the exit code is 0. */
#include "api/api.h"
#include "fs/fs.h"
#include "hal/hal.h"
#include "kernel/kernel.h"
#include "tos.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void out_str(const char *s)
{
    while (*s != '\0') {
        hal_con_out((uint8_t)*s);
        s++;
    }
}

static void out_u32(uint32_t v)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (v == 0u) {
        buf[--i] = '0';
    }
    while (v != 0u) {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    out_str(&buf[i]);
}

static const char *halt_name(uint8_t reason)
{
    switch (reason) {
    case TOS_HALT_NONE:       return "NONE";
    case TOS_HALT_HLT:        return "HLT";
    case TOS_HALT_COMMAND:    return "COMMAND";
    case TOS_HALT_EOF:        return "EOF";
    case TOS_HALT_TAPE_FAULT: return "TAPE_FAULT";
    case TOS_HALT_BREAKPOINT: return "BREAKPOINT";
    case TOS_HALT_BAD_TAPE:   return "BAD_TAPE";
    default:                  return "NONE";
    }
}

/* decimal or 0x-prefixed hex; 0 ok, -1 bad */
static int parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0u;
    if (s == NULL || *s == '\0') {
        return -1;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        if (*s == '\0') {
            return -1;
        }
        while (*s != '\0') {
            uint32_t d;
            if (*s >= '0' && *s <= '9') {
                d = (uint32_t)(*s - '0');
            } else if (*s >= 'a' && *s <= 'f') {
                d = (uint32_t)(*s - 'a' + 10);
            } else if (*s >= 'A' && *s <= 'F') {
                d = (uint32_t)(*s - 'A' + 10);
            } else {
                return -1;
            }
            v = (v << 4) | d;
            s++;
        }
        *out = v;
        return 0;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return 0;
}

/* "--name=value": returns 1 and sets *value when arg matches, else 0 */
static int opt_value(const char *arg, const char *name, const char **value)
{
    size_t n = strlen(name);
    if (strncmp(arg, name, n) == 0 && arg[n] == '=') {
        *value = arg + n + 1;
        return 1;
    }
    return 0;
}

static void usage(void)
{
    out_str("usage: turingos [--tapes=N] [--len=N] [--hz=N] [--seed=N] [--disks=N] [--disk=<a.img>]\n"
            "                [--disk-b=<b.img>] [--shell=<shell.com>] [--snap=N] [--snap-dir=<dir>]\n"
            "                [--input=console|keys] [--fps=N] [--display] [--raw=0|1]\n"
            "                [--stdin-script=<file>] [--trace] [--version]\n");
}

int main(int argc, char **argv)
{
    tos_config_t cfg;
    const char *disk_a = "build/disk/disk.img";
    const char *disk_b = NULL;
    const char *shell = NULL;
    const char *snap_dir = NULL;
    const char *fps = NULL;
    const char *raw = NULL;
    const char *stdin_script = NULL;
    int display = 0;
    int i;
    uint32_t t0;
    uint64_t c0;

    kernel_config_default(&cfg);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = NULL;
        uint32_t num = 0u;

        if (strcmp(a, "--version") == 0) {
            out_str("TuringOS " TOS_VERSION "\n");
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else if (strcmp(a, "--display") == 0) {
            display = 1;
        } else if (strcmp(a, "--trace") == 0) {
            cfg.trace = 1u;
        } else if (opt_value(a, "--tapes", &v)) {
            if (parse_u32(v, &num) != 0 || (num != 1u && num != 2u && num != 4u)) {
                out_str("turingos: --tapes must be 1, 2 or 4\n");
                return 1;
            }
            cfg.tapes = (uint8_t)num;
        } else if (opt_value(a, "--len", &v)) {
            if (parse_u32(v, &num) != 0 ||
                (num != TOS_TAPE_LEN_32K && num != TOS_TAPE_LEN_48K && num != TOS_TAPE_LEN_64K)) {
                out_str("turingos: --len must be 32768, 49152 or 65536\n");
                return 1;
            }
            cfg.tape_len = num;
        } else if (opt_value(a, "--hz", &v)) {
            if (parse_u32(v, &num) != 0) {
                out_str("turingos: bad --hz value\n");
                return 1;
            }
            cfg.hz = num;
        } else if (opt_value(a, "--snap", &v)) {
            if (parse_u32(v, &num) != 0) {
                out_str("turingos: bad --snap value (steps between snapshots, 0 = never)\n");
                return 1;
            }
            cfg.snap_interval = num;
        } else if (opt_value(a, "--input", &v)) {
            if (strcmp(v, "console") == 0) {
                cfg.input_mode = (uint8_t)TOS_INPUT_CONSOLE;
            } else if (strcmp(v, "keys") == 0) {
                cfg.input_mode = (uint8_t)TOS_INPUT_KEYS;
            } else {
                out_str("turingos: --input must be console or keys\n");
                return 1;
            }
        } else if (opt_value(a, "--seed", &v)) {
            if (parse_u32(v, &num) != 0 || num > 255u) {
                out_str("turingos: --seed must be 0..255\n");
                return 1;
            }
            cfg.seed = (uint8_t)num;
        } else if (opt_value(a, "--disks", &v)) {
            if (parse_u32(v, &num) != 0 || (num != 1u && num != 2u)) {
                out_str("turingos: --disks must be 1 or 2\n");
                return 1;
            }
            cfg.disks = (uint8_t)num;
        } else if (opt_value(a, "--disk-b", &v)) {
            disk_b = v;
            if (cfg.disks < 2u) {
                cfg.disks = 2u;
            }
        } else if (opt_value(a, "--disk", &v)) {
            disk_a = v;
        } else if (opt_value(a, "--shell", &v)) {
            shell = v;
        } else if (opt_value(a, "--snap-dir", &v)) {
            snap_dir = v;
        } else if (opt_value(a, "--fps", &v)) {
            fps = v;
        } else if (opt_value(a, "--display", &v)) {
            display = (v[0] == '0') ? 0 : 1;
        } else if (opt_value(a, "--raw", &v)) {
            raw = v;
        } else if (opt_value(a, "--stdin-script", &v)) {
            stdin_script = v;
        } else if (opt_value(a, "--trace", &v)) {
            cfg.trace = (v[0] == '0') ? 0u : 1u;
        } else {
            out_str("turingos: unknown option ");
            out_str(a);
            out_str("\n");
            usage();
            return 1;
        }
    }

    (void)hal_set_option("disk_a", disk_a);
    if (disk_b != NULL) {
        (void)hal_set_option("disk_b", disk_b);
    }
    if (shell != NULL) {
        (void)hal_set_option("shell", shell);
    }
    if (snap_dir != NULL) {
        (void)hal_set_option("snap_dir", snap_dir);
    }
    if (fps != NULL) {
        (void)hal_set_option("fps", fps);
    }
    (void)hal_set_option("display", display ? "1" : "0");
    if (raw != NULL) {
        (void)hal_set_option("raw", raw);
    }
    if (stdin_script != NULL) {
        (void)hal_set_option("stdin_script", stdin_script);
    }

    hal_init();
    if (tos_create(&cfg) != 0) {
        hal_shutdown();
        out_str("turingos: bad configuration\n");
        return 1;
    }

    /* kernel_run semantics through the API: step in slices until the machine halts. */
    t0 = hal_time_ms();
    c0 = ((uint64_t)tos_cycles_hi() << 32) | (uint64_t)tos_cycles_lo();
    for (;;) {
        uint32_t budget = 4096u;
        int stop;

        if (cfg.hz != 0u) {
            budget = cfg.hz / 420u + 1u;      /* about one 60 Hz frame of ~7-cycle instructions */
            if (budget > 65536u) {
                budget = 65536u;
            }
        }
        (void)tos_step(budget);
        stop = tos_stop_reason();
        if (stop == (int)KSTOP_HALT) {
            break;
        }
        if (stop == (int)KSTOP_VSYNC) {
            /* The frame the machine just finished: pace it here, not in the kernel. Fall through
             * to the clock throttle, or a program that stops every frame would never meet it. */
            hal_vsync();
        }
        if (stop == (int)KSTOP_WAIT_INPUT) {
            /* Only reached on a TTY with nothing typed: the posix HAL blocks inside
             * hal_con_in_ready() when stdin is a pipe or file, so scripts never spin here.
             * Waiting for a person is not emulated time, so the clock starts again afterwards. */
            hal_vsync();
            t0 = hal_time_ms();
            c0 = ((uint64_t)tos_cycles_hi() << 32) | (uint64_t)tos_cycles_lo();
            continue;
        }
        if (cfg.hz != 0u) {
            uint64_t cycles = ((uint64_t)tos_cycles_hi() << 32) | (uint64_t)tos_cycles_lo();
            uint64_t want_ms = (cycles - c0) * 1000u / (uint64_t)cfg.hz;
            uint32_t spent = hal_time_ms() - t0;
            if (want_ms > (uint64_t)spent) {
                hal_sleep_ms((uint32_t)(want_ms - (uint64_t)spent));
            }
        }
    }

    /* Sector writes and anything else that dirtied a disk image reach the host here, once,
     * rather than rewriting half a megabyte per sector. */
    fs_flush();
    hal_shutdown();
    /* The exit line always starts on its own line. Only the `halt` command is known to have
     * left the cursor at a line start ("HALT\n"); EOF and faults usually interrupt a prompt. */
    if (tos_halt_reason() != (uint8_t)TOS_HALT_COMMAND) {
        out_str("\n");
    }
    out_str("TuringOS halted (reason=");
    out_str(halt_name(tos_halt_reason()));
    out_str(") after ");
    out_u32(tos_steps());
    out_str(" steps\n");
    return 0;
}
