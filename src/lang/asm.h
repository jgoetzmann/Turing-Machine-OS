#ifndef TURINGOS_LANG_ASM_H
#define TURINGOS_LANG_ASM_H
#include <stdint.h>
/* 8080 assembler. Output is a flat .com image for ORG 0100H (default). Returns output length or -1;
 * on error writes "line N: message" into err. */
int asm_assemble(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap);
#endif
