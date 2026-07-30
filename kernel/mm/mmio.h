#ifndef JANI_KERNEL_MM_MMIO_H
#define JANI_KERNEL_MM_MMIO_H

#include <stdint.h>

void *mmio_map(uint64_t physical_address, uint64_t length);

#endif
