/* echo.c -- reads one line from the console with getchar() (BIOS CONIN) and
 * prints it back followed by a newline. Each getchar() with nothing pending
 * parks the machine in the IDLE state until a byte arrives.
 * Input "hello\n" -> output "hello\n" */

int main() {
    int c;
    c = getchar();
    while (c != '\n' && c != 13) {      /* stop at LF or CR */
        putchar(c);
        c = getchar();
    }
    putchar('\n');
    return 0;
}
