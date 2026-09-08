/* fault.c -- walks writes upward from 0x4000 until the tape ends.
 *
 * Every cell from 0x4000 up gets its low address byte, through the banked
 * window, the scratch area, the stack region, the display and the metadata
 * block. The only cells left alone are the top 256 bytes of the stack region
 * (L-0x300 .. L-0x201), which hold main's own frame and return address.
 * Every 4 KB the current address is printed. On a 32K or 48K tape the first
 * write at address L faults: the kernel finishes the STA, then halts with
 * TOS_HALT_TAPE_FAULT. On a 64K tape there is no such address: the walk
 * wraps past 0xFFFF and the program returns to the shell.
 */

char hex[17] = "0123456789ABCDEF";

int addr;
int skip_lo;    /* L - 0x300 */
int skip_hi;    /* L - 0x201 (initial SP) */

int print_hex(int v) {
    putchar(hex[(v >> 12) & 15]);
    putchar(hex[(v >> 8) & 15]);
    putchar(hex[(v >> 4) & 15]);
    putchar(hex[v & 15]);
    return 0;
}

int main() {
    int len = inp(5) << 8;              /* L as a 16-bit value: 0x8000, 0xC000 or 0 */
    skip_lo = len - 768;
    skip_hi = len - 513;
    addr = 0x4000;
    while (addr != 0) {
        if ((addr & 4095) == 0) {
            print_hex(addr);
            putchar('\n');
        }
        if (addr < skip_lo || addr > skip_hi) {
            poke(addr, addr & 255);
        }
        addr++;
    }
    puts("no fault: tape is 64K");
    return 0;
}
