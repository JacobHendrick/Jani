#include "serial.h"

#define COM1_PORT 0x3F8
#define COM1_DATA_PORT (COM1_PORT + 0)
#define COM1_INTERRUPT_ENABLE_PORT (COM1_PORT + 1)
#define COM1_FIFO_COMMAND_PORT (COM1_PORT + 2)
#define COM1_LINE_COMMAND_PORT (COM1_PORT + 3)
#define COM1_MODEM_COMMAND_PORT (COM1_PORT + 4)
#define COM1_LINE_STATUS_PORT (COM1_PORT + 5)

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int serial_transmit_ready(void) {
    return inb(COM1_LINE_STATUS_PORT) & 0x20;
}

void serial_init(void) {
    outb(COM1_INTERRUPT_ENABLE_PORT, 0x00);
    outb(COM1_LINE_COMMAND_PORT, 0x80);
    outb(COM1_DATA_PORT, 0x03);
    outb(COM1_INTERRUPT_ENABLE_PORT, 0x00);
    outb(COM1_LINE_COMMAND_PORT, 0x03);
    outb(COM1_FIFO_COMMAND_PORT, 0xC7);
    outb(COM1_MODEM_COMMAND_PORT, 0x0B);
}

void serial_write_char(char c) {
    if (c == '\n') {
        serial_write_char('\r');
    }

    while (!serial_transmit_ready()) {
    }

    outb(COM1_DATA_PORT, (unsigned char)c);
}

void serial_write_string(const char *str) {
    while (*str != '\0') {
        serial_write_char(*str);
        str++;
    }
}
