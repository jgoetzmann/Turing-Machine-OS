#ifndef TURINGOS_DISASM_H
#define TURINGOS_DISASM_H
#include <stdint.h>
/* Formats one instruction. `bytes` must point at >= 3 readable bytes.
 * Output: UPPERCASE mnemonic, one space, operands separated by "," (no spaces),
 * 8-bit immediates as two hex digits + "H", 16-bit as four hex digits + "H",
 * e.g. "MVI A,05H", "LXI H,1234H", "JMP 0123H", "MOV A,M", "RST 3", "NOP".
 * Undocumented aliases carry a "*": "NOP*", "JMP* 0123H", "RET*", "CALL* 0123H". Where one
 * alias covers several bytes the byte joins the spelling, so a listing re-assembles to the
 * bytes it came from: "NOP*10" ... "NOP*38", "CALL*ED 0123H", "CALL*FD 0123H"
 * (decisions.md B29).
 * Returns the instruction length (1..3). */
int disasm_one(const uint8_t *bytes, uint16_t addr, char *out, int cap);
#endif
