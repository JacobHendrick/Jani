#include "../arch/io.h"
#include "keyboard.h"

#define KEYBOARD_DATA_PORT 0x60

uint8_t keyboard_read_scancode(void) {
    return inb(KEYBOARD_DATA_PORT);
}