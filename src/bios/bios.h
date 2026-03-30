#ifndef TURINGOS_BIOS_H
#define TURINGOS_BIOS_H

#include "../emu/cpu.h"

void bios_init(void);
void bios_dispatch(cpu_t *cpu);
int bios_pending_output(void);
char bios_get_output(void);

#endif
