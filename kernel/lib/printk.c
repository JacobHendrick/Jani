#include <stdarg.h>
#include <stdint.h>

#include "printk.h"
#include "../drivers/serial.h"

static void print_unsigned(uint64_t value, uint32_t base) {
    char buffer[32];
    int index = 0;

    if (value == 0) {
        serial_write_char('0');
        return;
    }

    while (value > 0) {
        uint32_t digit = (uint32_t)(value % base);

        if (digit < 10) {
            buffer[index] = (char)('0' + digit);
        } else {
            buffer[index] = (char)('a' + (digit - 10));
        }

        index++;
        value /= base;
    }

    while (index > 0) {
        index--;
        serial_write_char(buffer[index]);
    }
}

static void print_signed(int64_t value) {
    uint64_t magnitude;

    if (value < 0) {
        serial_write_char('-');
        magnitude = (uint64_t)(-(value + 1)) + 1;
    } else {
        magnitude = (uint64_t)value;
    }

    print_unsigned(magnitude, 10);
}

void kputs(const char *str) {
    serial_write_string(str);
}

void printk(const char *format, ...) {
    va_list args;

    va_start(args, format);

    while (*format != '\0') {
        if (*format != '%') {
            serial_write_char(*format);
            format++;
            continue;
        }

        format++;

        if (*format == '\0') {
            break;
        }

        switch (*format) {
            case '%':
                serial_write_char('%');
                break;

            case 'c':
                serial_write_char((char)va_arg(args, int));
                break;

            case 's': {
                const char *str = va_arg(args, const char *);

                if (str == 0) {
                    str = "(null)";
                }

                serial_write_string(str);
                break;
            }

            case 'd':
                print_signed((int64_t)va_arg(args, int));
                break;

            case 'x':
                print_unsigned((uint64_t)va_arg(args, unsigned int), 16);
                break;

            case 'p':
                serial_write_string("0x");
                print_unsigned((uint64_t)(uintptr_t)va_arg(args, void *), 16);
                break;

            default:
                serial_write_char('%');
                serial_write_char(*format);
                break;
        }

        format++;
    }

    va_end(args);
}
