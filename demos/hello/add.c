/* add.c -- computes 3 + 4 in a function and prints the equation with
 * print_int. The arguments travel on the 8080 stack near the top of the tape.
 * Expected output: "3 + 4 = 7\n" */

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

int add(int a, int b) {
    return a + b;
}

int main() {
    int a = 3;
    int b = 4;
    int c = add(a, b);
    print_int(a);
    putchar(' ');
    putchar('+');
    putchar(' ');
    print_int(b);
    putchar(' ');
    putchar('=');
    putchar(' ');
    print_int(c);
    putchar('\n');
    return 0;
}
