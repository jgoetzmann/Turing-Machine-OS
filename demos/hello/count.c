/* count.c -- prints 1..10, one number per line, through a print_int helper.
 * A short spin loop between numbers gives the visualizer something to show.
 * Expected output: "1\n2\n...\n10\n" */

char digits[8];

/* Print a signed 16-bit int in decimal (handles negatives and -32768). */
int print_int(int n) {
    int i = 0;
    int d;
    if (n < 0) {
        putchar('-');
    }
    if (n == 0) {
        putchar('0');
        return 0;
    }
    while (n != 0) {
        d = n % 10;          /* truncates toward zero: remainder <= 0 for n < 0 */
        if (d < 0) d = -d;
        digits[i] = '0' + d;
        i++;
        n = n / 10;
    }
    while (i > 0) {
        i--;
        putchar(digits[i]);
    }
    return 0;
}

int pause() {
    int j = 0;
    while (j < 100) j++;
    return 0;
}

int main() {
    int i;
    for (i = 1; i <= 10; i++) {
        print_int(i);
        putchar('\n');
        pause();
    }
    return 0;
}
