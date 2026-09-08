/* dump_constants — prints every numeric TOS_* constant defined in src/tos.h (SPEC §S7, WS0-02).
 *
 *   dump_constants              sorted JSON object: {"NAME": value, ...}
 *   dump_constants --markdown   Markdown table: | Name | Hex | Dec |
 *
 * The tape-length-relative macros (TOS_BANK_END(L), TOS_META_BASE(L), ...) are evaluated at
 * 32768 / 49152 / 65536 and emitted as NAME_32K / NAME_48K / NAME_64K.
 * TOS_VERSION is a string and is deliberately not listed.
 * `make gen` writes the JSON form to build/gen/constants.json (copied to web/src/generated/);
 * tests/docs/test_constants.sh compares the Markdown form with the generated block in
 * docs/architecture.md. The table below is the explicit list: a new numeric macro in tos.h
 * must be added here by hand. */
#include "tos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char   *name;
    unsigned long value;
} constant_t;

#define C(name) { #name, (unsigned long)(name) }
#define L3(name)                                                   \
    { #name "_32K", (unsigned long)(name(TOS_TAPE_LEN_32K)) },     \
    { #name "_48K", (unsigned long)(name(TOS_TAPE_LEN_48K)) },     \
    { #name "_64K", (unsigned long)(name(TOS_TAPE_LEN_64K)) }

/* Every numeric macro in src/tos.h, in header order (sorted by name before printing). */
static constant_t table[] = {
    /* ---- Tape geometry ---- */
    C(TOS_TAPE_MAX),
    C(TOS_TAPES_MAX),
    C(TOS_TAPE_LEN_32K),
    C(TOS_TAPE_LEN_48K),
    C(TOS_TAPE_LEN_64K),

    /* ---- Fixed low regions ---- */
    C(TOS_BIOS_BASE),
    C(TOS_BIOS_END),
    C(TOS_TPA_BASE),
    C(TOS_TPA_END),
    C(TOS_TPA_SIZE),
    C(TOS_BANK_BASE),

    /* ---- Regions anchored to the top of the tape, per tape length ---- */
    L3(TOS_BANK_END),
    L3(TOS_SCRATCH_BASE),
    L3(TOS_SCRATCH_END),
    L3(TOS_STACK_BASE),
    L3(TOS_STACK_TOP),
    L3(TOS_DISPLAY_BASE),
    C(TOS_DISPLAY_SIZE),
    L3(TOS_META_BASE),
    C(TOS_META_SIZE),
    L3(TOS_DMA_DEFAULT),

    /* ---- Metadata block layout ---- */
    C(TOS_META_STATE),
    C(TOS_META_STEPS),
    C(TOS_META_HALT_REASON),
    C(TOS_META_TAPE_SEL),
    C(TOS_META_TAPE_COUNT),
    C(TOS_META_TAPE_PAGES),
    C(TOS_META_FRAME),
    C(TOS_META_KEYS),
    C(TOS_META_STOP),
    C(TOS_META_DIRTY),
    C(TOS_META_DIRTY_BYTES),
    C(TOS_META_SEED),
    C(TOS_META_HZ),
    C(TOS_META_INPUT_MODE),
    C(TOS_META_DISKS),
    C(TOS_META_TRACE),
    C(TOS_META_SYSCALL),
    C(TOS_META_SP_INIT),

    /* ---- Display ---- */
    C(TOS_DISPLAY_W),
    C(TOS_DISPLAY_H),

    /* ---- I/O ports ---- */
    C(TOS_PORT_BIOS),
    C(TOS_PORT_TAPE),
    C(TOS_PORT_KEYS),
    C(TOS_PORT_TAPES),
    C(TOS_PORT_PAGES),

    /* ---- BIOS function ids ---- */
    C(TOS_BIOS_CONIN),
    C(TOS_BIOS_CONOUT),
    C(TOS_BIOS_AUXOUT),
    C(TOS_BIOS_AUXIN),
    C(TOS_BIOS_CONST),
    C(TOS_BIOS_VSYNC),
    C(TOS_BIOS_RAND),
    C(TOS_BIOS_TICKS),
    C(TOS_BIOS_SELDISK),
    C(TOS_BIOS_SETTRK),
    C(TOS_BIOS_SETSEC),
    C(TOS_BIOS_SETDMA),
    C(TOS_BIOS_READ),
    C(TOS_BIOS_WRITE),
    C(TOS_BIOS_LISTDIR),
    C(TOS_BIOS_NAMECH),
    C(TOS_BIOS_TYPE),
    C(TOS_BIOS_RUN),
    C(TOS_BIOS_DEL),
    C(TOS_BIOS_CC),
    C(TOS_BIOS_READLINE),
    C(TOS_BIOS_LINEGET),
    C(TOS_BIOS_LINELEN),
    C(TOS_BIOS_ASM),
    C(TOS_BIOS_TM),
    C(TOS_BIOS_BF),

    /* ---- Halt reasons ---- */
    C(TOS_HALT_NONE),
    C(TOS_HALT_HLT),
    C(TOS_HALT_COMMAND),
    C(TOS_HALT_EOF),
    C(TOS_HALT_TAPE_FAULT),
    C(TOS_HALT_BREAKPOINT),
    C(TOS_HALT_BAD_TAPE),

    /* ---- Key bitmask ---- */
    C(TOS_KEY_W),
    C(TOS_KEY_S),
    C(TOS_KEY_UP),
    C(TOS_KEY_DOWN),
    C(TOS_KEY_SPACE),
    C(TOS_KEY_ESC),
    C(TOS_KEY_ENTER),
    C(TOS_KEY_ANY),

    /* ---- Levers ---- */
    C(TOS_LEVER_TAPES),
    C(TOS_LEVER_TAPE_LEN),
    C(TOS_LEVER_HZ),
    C(TOS_LEVER_SEED),
    C(TOS_LEVER_INPUT_MODE),
    C(TOS_LEVER_DISKS),
    C(TOS_LEVER_TRACE),
    C(TOS_LEVER_SNAP_INTERVAL),
    C(TOS_LEVER_COUNT),
    C(TOS_INPUT_CONSOLE),
    C(TOS_INPUT_KEYS),

    /* ---- Disk geometry ---- */
    C(TOS_DISK_TRACKS),
    C(TOS_DISK_SECTORS),
    C(TOS_DISK_SECTOR_BYTES),
    C(TOS_DISK_IMAGE_BYTES),
    C(TOS_DISK_DIR_ENTRIES),
    C(TOS_DISK_DIR_ENTRY),
    C(TOS_DISKS_MAX),

    /* ---- Compiler / language tool ids ---- */
    C(TOS_LANG_C),
    C(TOS_LANG_ASM),
    C(TOS_LANG_TM),
    C(TOS_LANG_BF)
};

#define TABLE_LEN (sizeof table / sizeof table[0])

static int by_name(const void *a, const void *b)
{
    const constant_t *ca = (const constant_t *)a;
    const constant_t *cb = (const constant_t *)b;
    return strcmp(ca->name, cb->name);
}

static void print_json(void)
{
    size_t i;
    printf("{\n");
    for (i = 0; i < TABLE_LEN; i++) {
        printf("  \"%s\": %lu%s\n", table[i].name, table[i].value,
               (i + 1 < TABLE_LEN) ? "," : "");
    }
    printf("}\n");
}

static void print_markdown(void)
{
    size_t i;
    printf("| Name | Hex | Dec |\n");
    printf("|---|---|---|\n");
    for (i = 0; i < TABLE_LEN; i++) {
        printf("| %s | 0x%04lX | %lu |\n", table[i].name, table[i].value, table[i].value);
    }
}

int main(int argc, char **argv)
{
    int markdown = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--markdown") == 0) {
            markdown = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            markdown = 0;
        } else {
            fprintf(stderr, "usage: dump_constants [--markdown|--json]\n");
            return 2;
        }
    }

    qsort(table, TABLE_LEN, sizeof table[0], by_name);

    if (markdown) {
        print_markdown();
    } else {
        print_json();
    }
    return 0;
}
