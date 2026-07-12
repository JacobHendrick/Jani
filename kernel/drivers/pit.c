#include "../arch/io.h"
#include "pit.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_FREQUENCY 1193182u

static volatile uint64_t pit_tick_count = 0;

void pit_init(uint32_t frequency_hz) {
    uint32_t divisor32;
    uint16_t divisor;

    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    divisor32 = PIT_BASE_FREQUENCY / frequency_hz;

    if (divisor32 == 0) {
        divisor32 = 1;
    }

    if (divisor32 > 0xFFFF) {
        divisor32 = 0xFFFF;
    }

    divisor = (uint16_t)divisor32;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_on_irq(void) {
    pit_tick_count++;
}

uint64_t pit_get_ticks(void) {
    return pit_tick_count;
}
