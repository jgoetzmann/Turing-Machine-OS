#ifndef TURINGOS_LANG_BF_H
#define TURINGOS_LANG_BF_H
#include <stdint.h>
/* Brainfuck -> 8080 .com. Returns length or -1; err = "line N: message" (unmatched brackets). */
int bf_compile(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap);
#endif
