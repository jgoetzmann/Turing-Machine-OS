/* memtest.c -- fills an int array with 1..10 (2 bytes per element, little
 * endian, in the program's data area) and sums it back.
 * Expected output: "sum=55\n" */

char digits[8];
int a[10];

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
        d = n % 10;
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

int main() {
    int i;
    int sum = 0;
    for (i = 0; i < 10; i++) {
        a[i] = i + 1;
    }
    for (i = 0; i < 10; i++) {
        sum += a[i];
    }
    putchar('s');
    putchar('u');
    putchar('m');
    putchar('=');
    print_int(sum);
    putchar('\n');
    return 0;
}
