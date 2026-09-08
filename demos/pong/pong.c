/* pong.c -- two-player Pong on the 64x32 one-bit display.
 *
 * Paddles are 1x6 at x=1 (player 1, keys W/S) and x=62 (player 2, keys
 * UP/DOWN; the CPU drives it at half speed until UP or DOWN is pressed).
 * The ball is one pixel. Scores are dots along row 0: player 1 from the
 * left, player 2 from the right. The first side to 5 ends the game and the
 * program returns to the shell (HLT). One vsync() per frame; rand() picks
 * the serve direction and angle; a serve waits 120 frames at the centre.
 *
 * Every frame touches only the pixels that changed, so a frame is a few
 * hundred 8080 instructions, never a full-screen redraw.
 */

char bitmask[8] = {128, 64, 32, 16, 8, 4, 2, 1};

int base;      /* display base address for the running tape length */
int p1y;       /* top row of paddle 1 (rows p1y..p1y+5) */
int p2y;       /* top row of paddle 2 */
int bx;        /* ball x (2..61) */
int by;        /* ball y (1..31); row 0 is the score row */
int dx;        /* ball velocity */
int dy;
int s1;        /* scores */
int s2;
int delay;     /* frames to hold the ball at the centre before a serve */
int human2;    /* 1 once UP or DOWN has been pressed */
int frame;     /* frame counter (CPU paddle moves on odd frames only) */

int setpix(int x, int y) {
    int a = base + (y << 3) + (x >> 3);
    poke(a, peek(a) | bitmask[x & 7]);
    return 0;
}

int clrpix(int x, int y) {
    int a = base + (y << 3) + (x >> 3);
    poke(a, peek(a) & ~bitmask[x & 7]);
    return 0;
}

/* Zero 32 framebuffer bytes (four rows) starting at byte offset off. */
int clear_rows(int off) {
    int a = base + off;
    int n = 0;
    while (n < 16) {
        pokew(a, 0);
        a += 2;
        n++;
    }
    return 0;
}

int draw_paddle(int x, int y) {
    int i = 0;
    while (i < 6) {
        setpix(x, y + i);
        i++;
    }
    return 0;
}

int draw_score() {
    int i = 0;
    while (i < s1) {
        setpix(2 + (i << 1), 0);
        i++;
    }
    i = 0;
    while (i < s2) {
        setpix(61 - (i << 1), 0);
        i++;
    }
    return 0;
}

int serve() {
    bx = 32;
    by = 16;
    if (rand() & 1) dx = 1; else dx = -1;
    dy = (rand() % 3) - 1;
    delay = 120;
    return 0;
}

/* Ball leaves a paddle with a vertical component that depends on where it hit. */
int deflect(int r) {
    if (r < 2) return -1;
    if (r > 3) return 1;
    return 0;
}

int move_p1(int k) {
    if ((k & 1) && p1y > 1) {            /* W: up */
        clrpix(1, p1y + 5);
        p1y--;
        setpix(1, p1y);
    } else if ((k & 2) && p1y < 26) {    /* S: down */
        clrpix(1, p1y);
        p1y++;
        setpix(1, p1y + 5);
    }
    return 0;
}

int move_p2(int k) {
    int up = 0;
    int down = 0;
    if (k & 12) human2 = 1;
    if (human2) {
        if (k & 4) up = 1;
        if (k & 8) down = 1;
    } else if (frame & 1) {              /* CPU player: half speed */
        if (dx > 0) {                    /* ball coming this way: track it */
            if (by < p2y + 2) up = 1;
            if (by > p2y + 3) down = 1;
        } else {                         /* otherwise drift back to the middle */
            if (p2y > 13) up = 1;
            if (p2y < 13) down = 1;
        }
    }
    if (up && p2y > 1) {
        clrpix(62, p2y + 5);
        p2y--;
        setpix(62, p2y);
    } else if (down && p2y < 26) {
        clrpix(62, p2y);
        p2y++;
        setpix(62, p2y + 5);
    }
    return 0;
}

int move_ball() {
    if (delay > 0) {
        delay--;
        return 0;
    }
    clrpix(bx, by);
    bx += dx;
    by += dy;
    if (by < 1) { by = 1; dy = 1; }
    if (by > 31) { by = 31; dy = -1; }
    if (bx == 2 && dx < 0) {             /* at paddle 1's face */
        if (by >= p1y && by < p1y + 6) {
            dx = 1;
            dy = deflect(by - p1y);
        } else {
            s2++;
            draw_score();
            serve();
        }
    }
    if (bx == 61 && dx > 0) {            /* at paddle 2's face */
        if (by >= p2y && by < p2y + 6) {
            dx = -1;
            dy = deflect(by - p2y);
        } else {
            s1++;
            draw_score();
            serve();
        }
    }
    setpix(bx, by);
    return 0;
}

int main() {
    int k;
    int off = 0;
    base = (inp(5) << 8) - 512;          /* TOS_DISPLAY_BASE(L) = L - 0x200 */
    p1y = 13;
    p2y = 13;
    s1 = 0;
    s2 = 0;
    human2 = 0;
    frame = 0;
    /* clear the framebuffer in eight cheap slices, one per frame */
    while (off < 256) {
        clear_rows(off);
        vsync();
        off += 32;
    }
    draw_paddle(1, p1y);
    draw_paddle(62, p2y);
    serve();
    setpix(bx, by);
    while (s1 < 5 && s2 < 5) {
        k = keys();
        move_p1(k);
        move_p2(k);
        move_ball();
        frame++;
        vsync();
    }
    return 0;
}
