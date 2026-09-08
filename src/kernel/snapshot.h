#ifndef TURINGOS_SNAPSHOT_H
#define TURINGOS_SNAPSHOT_H
#include "kernel.h"
#include <stdint.h>
/* Ring of full machine snapshots (all tapes + cpu + kernel + bios + fs state; NOT disk images, NOT ages, NOT trace). */
#define SNAPSHOT_SLOTS 32u
void     snapshot_reset(void);
int      snapshot_save(const kernel_t *k);        /* returns slot index; overwrites the oldest when full */
int      snapshot_restore(kernel_t *k, int slot); /* 0 ok, -1 bad slot */
int      snapshot_find(uint32_t step);            /* newest slot whose step <= `step`, or -1 */
int      snapshot_count(void);
uint32_t snapshot_step(int slot);                 /* step recorded in slot, 0 if empty */
#endif
