#ifndef TURINGOS_MEM_H
#define TURINGOS_MEM_H

#include <stdint.h>

typedef uint8_t tape_t;
typedef uint16_t addr_t;

void mem_init(void);
uint8_t mem_read(addr_t addr);
void mem_write(addr_t addr, uint8_t val);
uint8_t *mem_raw(void);

#endif
