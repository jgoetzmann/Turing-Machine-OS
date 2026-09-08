/* hello.c -- the smallest TuringOS program.
 * puts() prints the string literal followed by a newline; every character
 * leaves the machine through BIOS CONOUT (OUT 1 with A=2).
 * Expected output: "Hello, TuringOS!\n" */

int main() {
    puts("Hello, TuringOS!");
    return 0;
}
