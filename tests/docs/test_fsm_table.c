/* WS0-02, WS1-06: the transitions table in docs/architecture.md is the kernel's own table.
 *
 * The constants block in that document is generated and compared byte for byte; the FSM table is
 * hand-written prose, which is exactly the kind of table that drifts. This test does not generate
 * it: it checks that the document has one row per transition, in index order, naming the same two
 * states the kernel does. Reword the "fires when" column freely; renumber or re-point a transition
 * and this fails. */
#include "../testfw.h"
#include "kernel/kernel.h"

#include <stdio.h>
#include <string.h>

#define DOC     "docs/architecture.md"
#define MARKER  "### The 12 frozen transitions"
#define MAXLINE 1024

static const char *const STATE_NAMES[6] = { "BOOT", "IDLE", "SHELL", "RUNNING", "SYSCALL", "HALT" };

/* The row for transition i looks like "| i | FROM -> TO | ... |" with a real arrow in between. */
static int row_matches(const char *line, int index, const char *from, const char *to) {
    char want[16];
    const char *p;
    const char *f;
    const char *t;

    snprintf(want, sizeof want, "| %d |", index);
    if (strstr(line, want) != line) return 0;
    p = line + strlen(want);
    f = strstr(p, from);
    if (f == NULL) return 0;
    t = strstr(f + strlen(from), to);
    return t != NULL;
}

int main(void) {
    FILE *fp = fopen(DOC, "r");
    char line[MAXLINE];
    const kernel_transition_t *table;
    int count = 0;
    int i;
    int in_section = 0;
    int matched = 0;

    if (fp == NULL) {
        fprintf(stderr, "FAIL: cannot open %s (run from the repository root)\n", DOC);
        return 1;
    }
    table = kernel_transitions(&count);
    if (table == NULL || count != KERNEL_TRANSITION_COUNT) {
        fprintf(stderr, "FAIL: kernel_transitions returned %d rows\n", count);
        fclose(fp);
        return 1;
    }

    while (fgets(line, (int)sizeof line, fp) != NULL) {
        if (!in_section) {
            if (strstr(line, MARKER) != NULL) in_section = 1;
            continue;
        }
        if (line[0] == '|' && matched < count) {
            const kernel_transition_t *t = &table[matched];
            if (row_matches(line, matched, STATE_NAMES[t->from], STATE_NAMES[t->to])) {
                matched++;
            }
        }
        if (matched == count) break;
    }
    fclose(fp);

    if (!in_section) {
        fprintf(stderr, "FAIL: %s has no \"%s\" section\n", DOC, MARKER);
        return 1;
    }
    if (matched != count) {
        const kernel_transition_t *t = &table[matched];
        fprintf(stderr, "FAIL: %s row %d should read \"| %d | %s -> %s | ... |\" (the kernel's table)\n",
                DOC, matched, matched, STATE_NAMES[t->from], STATE_NAMES[t->to]);
        return 1;
    }
    for (i = 0; i < count; i++) {
        if (table[i].why == NULL || table[i].why[0] == '\0') {
            fprintf(stderr, "FAIL: transition %d has no reason string\n", i);
            return 1;
        }
    }
    printf("PASS: test_fsm_table\n");
    return 0;
}
