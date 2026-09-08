/* forth.c - a small Forth interpreter for TuringOS, written in tiny-C v2.
 *
 * Cells are 16-bit ints.  Every large structure lives in the bank window of
 * tape 0 (0x4000 upward, selected with tape(0)) so the compiled image stays
 * small and the whole interpreter state is visible on the tape map:
 *
 *   0x4000  ds[64]      data stack
 *   0x4080  rs[64]      return stack (also holds do/loop limit+index)
 *   0x4100  dnm[128]    dictionary: offset of each name in names[]
 *   0x4200  dln[128]    dictionary: name length
 *   0x4280  dcd[128]    dictionary: cell (primitive id, or 100 + body address)
 *   0x4400  names[1024] name characters
 *   0x4800  code[2048]  threaded code cells (0x4800..0x57FF)
 *   0x5800  line/tok/dbuf scratch; 0x5900.. is free for @ ! experiments
 *
 * Everything stays below 0x6000, so it also fits the 8 KB window of a 32K
 * tape.
 *
 * A code cell c is: 0..99 a primitive id, >= 100 a call to the body that
 * starts at code cell c-100.  Internal (unnamed) primitives: 40 LIT n,
 * 41 BRANCH a, 42 0BRANCH a, 43 EXIT, 44 (DO), 45 (LOOP) a.
 *
 * REPL: prints the prompt "ok\n", reads a line, interprets it, and repeats.
 * "." prints a number followed by a space.  An unknown word prints
 * "? name\n" and drops the rest of the line.  "bye" returns from main; the
 * post-main HLT hands control back to the shell.
 *
 * Session `: sq dup * ;` / `7 sq .` / `bye` prints "ok\nok\n49 ok\n".
 */

__at(0x4000) int ds[64];
__at(0x4080) int rs[64];
__at(0x4100) int dnm[128];
__at(0x4200) char dln[128];
__at(0x4280) int dcd[128];
__at(0x4400) char names[1024];
__at(0x4800) int code[2048];
__at(0x5800) char line[160];
__at(0x58A0) char tok[32];
__at(0x58C0) char dbuf[8];

/* primitive ids are the position of each name in this list (0..35) */
char boot[160] = "+ - * / mod dup drop swap over rot . emit key cr @ ! c@ c! : ; if else then begin until do loop i bye words = < > and or negate";

int dsp;
int rsp;
int ndict;
int nlen;
int here;
int state;
int err;
int quit;
int ip;
int lpos;
int llen;
int tlen;
int nval;

int push(int v) {
    if (dsp >= 64) { err = 2; return 0; }
    ds[dsp] = v;
    dsp = dsp + 1;
    return 0;
}

int pop() {
    if (dsp <= 0) { err = 2; return 0; }
    dsp = dsp - 1;
    return ds[dsp];
}

/* print n in decimal (no trailing space; "." adds it) */
int prnum(int n) {
    int i = 0;
    int d;
    if (n == 0) { putchar('0'); return 0; }
    if (n < 0) { putchar('-'); }
    while (n != 0) {
        d = n % 10;
        if (d < 0) { d = -d; }
        dbuf[i] = '0' + d;
        i = i + 1;
        n = n / 10;
    }
    while (i > 0) {
        i = i - 1;
        putchar(dbuf[i]);
    }
    return 0;
}

/* next blank-delimited token from line[] into tok[]; returns its length */
int word() {
    tlen = 0;
    while (lpos < llen && line[lpos] <= 32) { lpos = lpos + 1; }
    while (lpos < llen && line[lpos] > 32) {
        if (tlen < 31) { tok[tlen] = line[lpos]; tlen = tlen + 1; }
        lpos = lpos + 1;
    }
    return tlen;
}

/* newest definition first; -1 when tok[] is not a word */
int find() {
    int i = ndict - 1;
    int j;
    int o;
    while (i >= 0) {
        if (dln[i] == tlen) {
            o = dnm[i];
            j = 0;
            while (j < tlen && names[o + j] == tok[j]) { j = j + 1; }
            if (j == tlen) { return i; }
        }
        i = i - 1;
    }
    return -1;
}

/* add tok[] to the dictionary with the given code cell */
int create(int cell) {
    int j = 0;
    if (ndict >= 128 || nlen + tlen > 1024) { err = 3; return 0; }
    dnm[ndict] = nlen;
    dln[ndict] = tlen;
    dcd[ndict] = cell;
    while (j < tlen) {
        names[nlen] = tok[j];
        nlen = nlen + 1;
        j = j + 1;
    }
    ndict = ndict + 1;
    return 0;
}

/* decimal, optional leading '-'; sets nval and returns 1 when tok[] is a number */
int number() {
    int i = 0;
    int neg = 0;
    int v = 0;
    if (tok[0] == '-') { neg = 1; i = 1; }
    if (i >= tlen) { return 0; }
    while (i < tlen) {
        if (tok[i] < '0' || tok[i] > '9') { return 0; }
        v = v * 10 + (tok[i] - '0');
        i = i + 1;
    }
    if (neg) { v = -v; }
    nval = v;
    return 1;
}

/* append one cell of threaded code */
int comp(int v) {
    if (here >= 2046) { err = 3; return 0; }
    code[here] = v;
    here = here + 1;
    return 0;
}

/* list every word, newest first, one line */
int words() {
    int i = ndict - 1;
    int j;
    int o;
    while (i >= 0) {
        o = dnm[i];
        j = 0;
        while (j < dln[i]) {
            putchar(names[o + j]);
            j = j + 1;
        }
        putchar(' ');
        i = i - 1;
    }
    putchar('\n');
    return 0;
}

/* execute named primitive p (ids follow the boot string) */
int prim(int p) {
    int a;
    int b;
    int c;
    if (p <= 4) {
        a = pop();
        b = pop();
        if (p == 0) { push(b + a); }
        else if (p == 1) { push(b - a); }
        else if (p == 2) { push(b * a); }
        else if (p == 3) { push(b / a); }
        else { push(b % a); }
    }
    else if (p == 5) { a = pop(); push(a); push(a); }
    else if (p == 6) { pop(); }
    else if (p == 7) { a = pop(); b = pop(); push(a); push(b); }
    else if (p == 8) { a = pop(); b = pop(); push(b); push(a); push(b); }
    else if (p == 9) { a = pop(); b = pop(); c = pop(); push(b); push(a); push(c); }
    else if (p == 10) { prnum(pop()); putchar(' '); }
    else if (p == 11) { putchar(pop() & 255); }
    else if (p == 12) { push(getchar()); }
    else if (p == 13) { putchar('\n'); }
    else if (p == 14) { push(peekw(pop())); }
    else if (p == 15) { a = pop(); b = pop(); pokew(a, b); }
    else if (p == 16) { push(peek(pop())); }
    else if (p == 17) { a = pop(); b = pop(); poke(a, b & 255); }
    else if (p == 27) { if (rsp < 2) { err = 2; } else { push(rs[rsp - 1]); } }
    else if (p == 28) { quit = 1; }
    else if (p == 29) { words(); }
    else if (p >= 30 && p <= 34) {
        a = pop();
        b = pop();
        if (p == 30) { if (b == a) { push(-1); } else { push(0); } }
        else if (p == 31) { if (b < a) { push(-1); } else { push(0); } }
        else if (p == 32) { if (b > a) { push(-1); } else { push(0); } }
        else if (p == 33) { push(b & a); }
        else { push(b | a); }
    }
    else if (p == 35) { push(-pop()); }
    return 0;
}

/* inner interpreter: run threaded code from start until its EXIT */
int run(int start) {
    int c;
    int a;
    int b;
    int base = rsp;
    ip = start;
    while (err == 0 && quit == 0) {
        c = code[ip];
        ip = ip + 1;
        if (c >= 100) {
            if (rsp >= 64) { err = 2; return 0; }
            rs[rsp] = ip;
            rsp = rsp + 1;
            ip = c - 100;
        }
        else if (c == 43) {
            if (rsp <= base) { return 0; }
            rsp = rsp - 1;
            ip = rs[rsp];
        }
        else if (c == 40) { push(code[ip]); ip = ip + 1; }
        else if (c == 41) { ip = code[ip]; }
        else if (c == 42) {
            if (pop() == 0) { ip = code[ip]; } else { ip = ip + 1; }
        }
        else if (c == 44) {
            a = pop();
            b = pop();
            if (rsp >= 63) { err = 2; return 0; }
            rs[rsp] = b;
            rs[rsp + 1] = a;
            rsp = rsp + 2;
        }
        else if (c == 45) {
            if (rsp < 2) { err = 2; return 0; }
            rs[rsp - 1] = rs[rsp - 1] + 1;
            if (rs[rsp - 1] < rs[rsp - 2]) { ip = code[ip]; }
            else { rsp = rsp - 2; ip = ip + 1; }
        }
        else { prim(c); }
    }
    return 0;
}

/* ":" - start a definition named by the next token */
int colon() {
    if (word() == 0) { tok[0] = ':'; tlen = 1; err = 1; return 0; }
    create(100 + here);
    if (err == 0) { state = 1; }
    return 0;
}

/* interpret mode: execute dictionary entry d */
int execw(int d) {
    int cell = dcd[d];
    if (cell >= 100) { run(cell - 100); }
    else if (cell == 18) { colon(); }
    else if (cell >= 19 && cell <= 26) { err = 1; }
    else { prim(cell); }
    return 0;
}

/* compile mode: compile entry d, or act on the compiling words */
int compw(int d) {
    int cell = dcd[d];
    int a;
    if (cell == 19) { comp(43); state = 0; }
    else if (cell == 20) { comp(42); push(here); comp(0); }
    else if (cell == 21) { a = pop(); comp(41); push(here); comp(0); code[a] = here; }
    else if (cell == 22) { a = pop(); code[a] = here; }
    else if (cell == 23) { push(here); }
    else if (cell == 24) { comp(42); comp(pop()); }
    else if (cell == 25) { comp(44); push(here); }
    else if (cell == 26) { comp(45); comp(pop()); }
    else if (cell == 18) { err = 1; }
    else { comp(cell); }
    return 0;
}

/* print the error, undo a half-built definition, clear both stacks */
int report() {
    int j = 0;
    putchar('?');
    putchar(' ');
    if (err == 1) {
        while (j < tlen) {
            putchar(tok[j]);
            j = j + 1;
        }
        putchar('\n');
    }
    else if (err == 2) { puts("stack"); }
    else { puts("full"); }
    if (state) {
        ndict = ndict - 1;
        nlen = dnm[ndict];
        here = dcd[ndict] - 100;
    }
    dsp = 0;
    rsp = 0;
    state = 0;
    err = 0;
    return 0;
}

/* outer interpreter over line[] */
int interpret() {
    int d;
    while (err == 0 && quit == 0) {
        if (word() == 0) { return 0; }
        d = find();
        if (d >= 0) {
            if (state) { compw(d); } else { execw(d); }
        }
        else if (number()) {
            if (state) { comp(40); comp(nval); } else { push(nval); }
        }
        else { err = 1; }
    }
    if (err) { report(); }
    return 0;
}

/* read one line (LF ends it, CR ignored, no echo) into line[] */
int readln() {
    int c;
    llen = 0;
    while (1) {
        c = getchar();
        if (c == 10 || c == 0) { return 0; }
        if (c != 13 && llen < 159) {
            line[llen] = c;
            llen = llen + 1;
        }
    }
    return 0;
}

/* build the dictionary of primitives from the boot string */
int init() {
    int n = 0;
    dsp = 0;
    rsp = 0;
    ndict = 0;
    nlen = 0;
    here = 0;
    state = 0;
    err = 0;
    quit = 0;
    llen = 0;
    while (boot[llen] != 0) {
        line[llen] = boot[llen];
        llen = llen + 1;
    }
    lpos = 0;
    while (word()) {
        create(n);
        n = n + 1;
    }
    return 0;
}

int main() {
    tape(0);
    init();
    while (quit == 0) {
        puts("ok");
        readln();
        lpos = 0;
        interpret();
    }
    return 0;
}
