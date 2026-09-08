#ifndef TURINGOS_LANG_TM_H
#define TURINGOS_LANG_TM_H
#include <stdint.h>
/* Turing-machine description language -> 8080 .com (see SPEC §TM). Returns length or -1; err = "line N: message". */
int tm_compile(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap);
#endif
