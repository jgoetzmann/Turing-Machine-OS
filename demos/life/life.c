/* life.c -- Conway's Game of Life on the 64x32 display with wrapping edges.
 *
 * Cells live in the banked window (one byte per cell, row-major, 64 wide) at
 * 0x4000, so the tape map shows the board itself. The board starts with an
 * R-pentomino whose bounding box has its top-left corner at (30,15):
 *
 *     .XX        cells (31,15) (32,15)
 *     XX.              (30,16) (31,16)
 *     .X.              (31,17)
 *
 * One generation per vsync(): after the Nth vsync the display holds
 * generation N (gen100.expected is the framebuffer after the 100th).
 * SPACE reseeds the board from rand(); ESC returns to the shell.
 *
 * The generation runs in place, row by row: prev[] keeps the old values of
 * the row above, row0[] the old first row (the last row's lower neighbour),
 * cs[] holds three-row column sums with wrap padding, and a running
 * three-column window gives each cell's neighbour count with two array
 * reads. Every hot variable is a global (one LHLD instead of an SP-relative
 * load) and there are no shifts, multiplies or comparisons in the inner loop:
 * the B3/S23 rule is a table lookup and the framebuffer byte is built by
 * doubling. The new row is written straight into cells[] and the display.
 */

__at(0x4000) char cells[2048];
char prev[64];
char row0[64];
char cs[68];
char rule0[10];   /* dead cell: born when it has 3 neighbours */
char rule1[10];   /* live cell: survives with 2 or 3 neighbours */

int base;      /* display base address */
int gen;       /* generation counter */
int y;
int x;
int b;         /* row base = y * 64 */
int d;         /* base of the row below */
int bx;        /* running index into the current row */
int dx;        /* running index into the row below */
int a;         /* display byte address */
int n;         /* window sum: neighbours + centre */
int c;         /* the cell being decided (old value) */
int v;         /* its new value */
int k;
int g;
int pb;        /* framebuffer byte under construction */
int i;

int clear_cells() {
    i = 0;
    while (i < 2048) {
        cells[i] = 0;
        i++;
    }
    return 0;
}

int seed_pentomino() {
    cells[15 * 64 + 31] = 1;
    cells[15 * 64 + 32] = 1;
    cells[16 * 64 + 30] = 1;
    cells[16 * 64 + 31] = 1;
    cells[17 * 64 + 31] = 1;
    return 0;
}

int seed_random() {
    i = 0;
    while (i < 2048) {
        cells[i] = rand() & 1;
        i++;
    }
    return 0;
}

/* Pack cells[] into the 1-bpp framebuffer: 8 cells per byte, MSB first. */
int render() {
    a = base;
    i = 0;
    while (i < 2048) {
        pb = 0;
        k = 0;
        while (k < 8) {
            pb = pb + pb + cells[i];
            i++;
            k++;
        }
        poke(a, pb);
        a++;
    }
    return 0;
}

/* One generation: B3/S23 on a torus, written into cells[] and the display. */
int generation() {
    x = 0;
    k = 64;
    while (k) {
        prev[x] = cells[1984 + x];      /* old row 31 is row 0's upper neighbour */
        row0[x] = cells[x];             /* old row 0 is row 31's lower neighbour */
        x++;
        k--;
    }
    a = base;
    b = 0;
    y = 32;
    while (y) {
        d = b + 64;
        /* column sums of the three rows around this row; save its old values into prev[] */
        bx = b;
        dx = d;
        x = 0;
        k = 64;
        if (y > 1) {
            while (k) {
                c = cells[bx];
                cs[x + 1] = prev[x] + c + cells[dx];
                prev[x] = c;
                x++;
                bx++;
                dx++;
                k--;
            }
        } else {
            while (k) {
                c = cells[bx];
                cs[x + 1] = prev[x] + c + row0[x];
                prev[x] = c;
                x++;
                bx++;
                k--;
            }
        }
        cs[0] = cs[64];
        cs[65] = cs[1];
        n = cs[0] + cs[1] + cs[2];
        x = 0;
        bx = b;
        g = 8;
        while (g) {
            pb = 0;
            k = 8;
            while (k) {
                c = cells[bx];
                if (c) v = rule1[n - 1]; else v = rule0[n];
                cells[bx] = v;
                pb = pb + pb + v;
                n = n + cs[x + 3] - cs[x];
                x++;
                bx++;
                k--;
            }
            poke(a, pb);
            a++;
            g--;
        }
        b = d;
        y--;
    }
    gen++;
    return 0;
}

int main() {
    int kk;
    base = (inp(5) << 8) - 512;          /* TOS_DISPLAY_BASE(L) = L - 0x200 */
    gen = 0;
    i = 0;
    while (i < 10) { rule0[i] = 0; rule1[i] = 0; i++; }
    rule0[3] = 1;
    rule1[2] = 1;
    rule1[3] = 1;
    clear_cells();
    seed_pentomino();
    render();
    while (1) {
        kk = keys();
        if (kk & 32) return 0;           /* ESC: back to the shell */
        if (kk & 16) seed_random();      /* SPACE: random soup from rand() */
        generation();
        vsync();
    }
    return 0;
}
