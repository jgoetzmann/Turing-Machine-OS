#include "kernel/kernel.h"

#include <stdio.h>

int main(void) {
    kernel_t kernel;
    kernel_init(&kernel);
    kernel_run(&kernel);
    printf("TuringOS stub boot complete (state=%d)\n", (int)kernel_state(&kernel));
    return 0;
}
