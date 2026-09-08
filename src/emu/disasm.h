#ifndef TURINGOS_DISASM_H
#define TURINGOS_DISASM_H
#include <stdint.h>
/* Formats one instruction. `bytes` must point at >= 3 readable bytes.
 * Output: UPPERCASE mnemonic, one space, operands separated by "," (no spaces),
 * 8-bit immediates as two hex digits + "H", 16-bit as four hex digits + "H",
 * e.g. "MVI A,05H", "LXI H,1234H", "JMP 0123H", "MOV A,M", "RST 3", "NOP".
 * Undocumented aliases get a trailing "*": "NOP*", "JMP* 0123H", "RET*", "CALL* 0123H".
 * Returns the instruction length (1..3). */
int disasm_one(const uint8_t *bytes, uint16_t addr, char *out, int cap);
#endif
