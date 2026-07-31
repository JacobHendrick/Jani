#ifndef JANI_KERNEL_DRIVERS_PIT_H
#define JANI_KERNEL_DRIVERS_PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency_hz);
void pit_on_irq(void);
uint64_t pit_get_ticks(void);

/* Microseconds since pit_init, interpolated inside the current tick by
 * latching channel 0's counter. Resolution is one PIT cycle (~0.84 us)
 * rather than one tick (10 ms at 100 Hz). Returns 0 before pit_init. */
uint64_t pit_get_microseconds(void);

#endif