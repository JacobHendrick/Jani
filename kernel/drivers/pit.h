#ifndef JANI_KERNEL_DRIVERS_PIT_H
#define JANI_KERNEL_DRIVERS_PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency_hz);
void pit_on_irq(void);
uint64_t pit_get_ticks(void);

#endif