/* TuringOS v2 shell — tiny-C v2, runs on the 8080 in the TPA at 0x0100 (SPEC §S2 "Shell").
 *
 * Built by build/cc_driver into build/bin/shell.com (embedded as the shell blob) and shipped on the
 * demo disk as SHELL.C, where `cc SHELL.C` reproduces the same bytes. Prompt "A> " (or "B> " after
 * `disk b`). Commands: dir, type F, run F, cc F, asm F, tm F, bf F, del F, cls, mem, disk a|b,
 * halt, help. Unknown or malformed input prints "?". Leading spaces are ignored; a blank line just
 * re-prompts. `halt` prints "HALT" and returns from main; the post-main HLT halts the machine in
 * SHELL state (TOS_HALT_COMMAND).
 *
 * Every identifier is prefixed sh_ and parameter lists use `(void)`, which tiny-C accepts as
 * "no parameters". */

char sh_line[128];
int sh_len;
int sh_pos;
int sh_disk;

/* Reads one console line into sh_line (NUL-terminated, at most 127 bytes). */
int sh_fill(void) {
    int i;
    readline();
    sh_len = linelen();
    if (sh_len > 127) {
        sh_len = 127;
    }
    i = 0;
    while (i < sh_len) {
        sh_line[i] = lineget(i);
        i++;
    }
    sh_line[sh_len] = 0;
    return sh_len;
}

int sh_skipsp(void) {
    while (sh_line[sh_pos] == ' ') {
        sh_pos++;
    }
    return 0;
}

int sh_atend(void) {
    sh_skipsp();
    return sh_line[sh_pos] == 0;
}

/* Matches the command word at sh_pos (2..4 letters; c/d are 0 when the word is shorter) and
 * consumes it. The word must be followed by a space or the end of the line. */
int sh_word(char a, char b, char c, char d) {
    int n;
    if (sh_line[sh_pos] != a) {
        return 0;
    }
    if (sh_line[sh_pos + 1] != b) {
        return 0;
    }
    n = 2;
    if (c) {
        if (sh_line[sh_pos + 2] != c) {
            return 0;
        }
        n = 3;
        if (d) {
            if (sh_line[sh_pos + 3] != d) {
                return 0;
            }
            n = 4;
        }
    }
    if (sh_line[sh_pos + n] != 0 && sh_line[sh_pos + n] != ' ') {
        return 0;
    }
    sh_pos += n;
    return 1;
}

/* Pushes the single remaining word through namech(); 0 when it is missing or followed by junk. */
int sh_sendname(void) {
    int start;
    int end;
    int i;
    sh_skipsp();
    start = sh_pos;
    while (sh_line[sh_pos] != 0 && sh_line[sh_pos] != ' ') {
        sh_pos++;
    }
    end = sh_pos;
    if (end == start) {
        return 0;
    }
    if (!sh_atend()) {
        return 0;
    }
    i = start;
    while (i < end) {
        namech(sh_line[i]);
        i++;
    }
    return 1;
}

int sh_bad(void) {
    putchar('?');
    putchar(10);
    return 0;
}

/* Prints v as four upper-case hex digits. */
int sh_hex4(int v) {
    int i;
    int d;
    i = 12;
    while (i >= 0) {
        d = (v >> i) & 15;
        if (d < 10) {
            putchar('0' + d);
        } else {
            putchar('A' + d - 10);
        }
        i -= 4;
    }
    return 0;
}

int sh_region(int lo, int hi) {
    sh_hex4(lo);
    putchar('-');
    sh_hex4(hi);
    putchar(' ');
    return 0;
}

/* One "XXXX-YYYY NAME" line per region of the memory map for the running tape length.
 * inp(5) = (L / 256) & 0xFF, so top = L as a 16-bit value (0 for 64K, which wraps correctly). */
int sh_mem(void) {
    int top;
    top = inp(5) << 8;
    sh_region(0, 0xFF);
    puts("BIOS");
    sh_region(0x100, 0x3FFF);
    puts("TPA");
    sh_region(0x4000, top - 0x2001);
    puts("BANK");
    sh_region(top - 0x2000, top - 0x1001);
    puts("SCRATCH");
    sh_region(top - 0x1000, top - 0x201);
    puts("STACK");
    sh_region(top - 0x200, top - 0x101);
    puts("DISPLAY");
    sh_region(top - 0x100, top - 1);
    puts("META");
    return 0;
}

/* ESC[2J ESC[H */
int sh_cls(void) {
    putchar(27);
    putchar('[');
    putchar('2');
    putchar('J');
    putchar(27);
    putchar('[');
    putchar('H');
    return 0;
}

/* disk a | disk b */
int sh_diskcmd(void) {
    int d;
    sh_skipsp();
    d = sh_line[sh_pos];
    if (d == 'a' || d == 'A') {
        d = 0;
    } else if (d == 'b' || d == 'B') {
        d = 1;
    } else {
        return sh_bad();
    }
    sh_pos++;
    if (!sh_atend()) {
        return sh_bad();
    }
    /* SELDISK fails when that disk is not mounted; keeping the prompt honest matters more than
       pretending the switch worked. */
    if (seldisk(d) != 0) {
        return sh_bad();
    }
    sh_disk = d;
    return 0;
}

/* Runs one command line; returns 1 when the shell should exit (halt). */
int sh_dispatch(void) {
    sh_pos = 0;
    sh_skipsp();
    if (sh_line[sh_pos] == 0) {
        return 0;
    }
    if (sh_word('d', 'i', 'r', 0)) {
        if (!sh_atend()) {
            return sh_bad();
        }
        listdir();
        return 0;
    }
    if (sh_word('t', 'y', 'p', 'e')) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        namend();
        return 0;
    }
    if (sh_word('r', 'u', 'n', 0)) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        runend();
        return 0;
    }
    if (sh_word('c', 'c', 0, 0)) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        ccend();
        return 0;
    }
    if (sh_word('a', 's', 'm', 0)) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        asmend();
        return 0;
    }
    if (sh_word('t', 'm', 0, 0)) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        tmend();
        return 0;
    }
    if (sh_word('b', 'f', 0, 0)) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        bfend();
        return 0;
    }
    if (sh_word('d', 'e', 'l', 0)) {
        if (!sh_sendname()) {
            return sh_bad();
        }
        delend();
        return 0;
    }
    if (sh_word('c', 'l', 's', 0)) {
        if (!sh_atend()) {
            return sh_bad();
        }
        sh_cls();
        return 0;
    }
    if (sh_word('m', 'e', 'm', 0)) {
        if (!sh_atend()) {
            return sh_bad();
        }
        sh_mem();
        return 0;
    }
    if (sh_word('d', 'i', 's', 'k')) {
        sh_diskcmd();
        return 0;
    }
    if (sh_word('h', 'a', 'l', 't')) {
        if (!sh_atend()) {
            return sh_bad();
        }
        puts("HALT");
        return 1;
    }
    if (sh_word('h', 'e', 'l', 'p')) {
        if (!sh_atend()) {
            return sh_bad();
        }
        puts("dir type run cc asm tm bf del cls mem disk halt help");
        return 0;
    }
    return sh_bad();
}

int main(void) {
    sh_disk = 0;
    while (1) {
        putchar('A' + sh_disk);
        putchar('>');
        putchar(' ');
        sh_fill();
        if (sh_dispatch()) {
            return 0;
        }
    }
    return 0;
}
