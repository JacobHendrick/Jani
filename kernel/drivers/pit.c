#include "../arch/io.h"
#include "pit.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_FREQUENCY 1193182u

#define PIT_LATCH_CHANNEL0 0x00
#define PIT_READ_ATTEMPTS 3

static volatile uint64_t pit_tick_count = 0;
static uint16_t pit_divisor_value = 0;

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
    pit_divisor_value = divisor;

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

static uint64_t pit_cycles_to_microseconds(uint64_t cycles) {
    uint64_t whole_seconds;
    uint64_t remainder;

    whole_seconds = cycles / PIT_BASE_FREQUENCY;
    remainder = cycles % PIT_BASE_FREQUENCY;

    return (whole_seconds * 1000000u)
        + ((remainder * 1000000u) / PIT_BASE_FREQUENCY);
}

uint64_t pit_get_microseconds(void) {
    int attempt;

    if (pit_divisor_value == 0) {
        return 0;
    }

    for (attempt = 0; attempt < PIT_READ_ATTEMPTS; attempt++) {
        uint64_t ticks_before;
        uint64_t ticks_after;
        uint16_t counter;
        uint16_t elapsed;

        ticks_before = pit_tick_count;

        outb(PIT_COMMAND, PIT_LATCH_CHANNEL0);
        counter = (uint16_t)inb(PIT_CHANNEL0);
        counter = (uint16_t)(counter | ((uint16_t)inb(PIT_CHANNEL0) << 8));

        ticks_after = pit_tick_count;

        if (ticks_before != ticks_after) {
            continue;
        }

        if (counter > pit_divisor_value) {
            elapsed = 0;
        } else {
            elapsed = (uint16_t)(pit_divisor_value - counter);
        }

        return pit_cycles_to_microseconds(
            (ticks_after * (uint64_t)pit_divisor_value) + (uint64_t)elapsed);
    }

    return pit_cycles_to_microseconds(pit_tick_count
                                      * (uint64_t)pit_divisor_value);
}
