#include "jani_libc.h"

#ifdef JANI_HOSTED
#include <stdio.h>
#else
#include "../../lib/printk.h"
#endif

#define SINK_DIGIT_CAPACITY 24u
#define DOUBLE_FRACTION_DIGITS 6u
#define DOUBLE_MAX_WHOLE 18446744073709551615.0

struct sink {
    char *buffer;
    size_t size;
    size_t written;
};

static void sink_put(struct sink *target, char character) {
    if ((target->size > 0) && (target->written < (target->size - 1u))) {
        target->buffer[target->written] = character;
    }
    target->written++;
}

static void sink_string(struct sink *target, const char *text) {
    size_t index;

    for (index = 0; text[index] != '\0'; index++) {
        sink_put(target, text[index]);
    }
}

static void sink_unsigned(
    struct sink *target,
    unsigned long long value,
    unsigned base,
    int uppercase
) {
    const char *alphabet;
    char digits[SINK_DIGIT_CAPACITY];
    size_t count;

    alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0) {
        sink_put(target, '0');
        return;
    }

    count = 0;
    while ((value != 0) && (count < SINK_DIGIT_CAPACITY)) {
        digits[count] = alphabet[value % (unsigned long long)base];
        value /= (unsigned long long)base;
        count++;
    }

    while (count > 0) {
        count--;
        sink_put(target, digits[count]);
    }
}

static void sink_signed(struct sink *target, long long value) {
    unsigned long long magnitude;

    if (value < 0) {
        sink_put(target, '-');
        magnitude = (unsigned long long)(-(value + 1)) + 1ull;
    } else {
        magnitude = (unsigned long long)value;
    }

    sink_unsigned(target, magnitude, 10u, 0);
}

static void sink_double(struct sink *target, double value) {
    unsigned long long whole;
    double fraction;
    unsigned index;
    unsigned digit;

    if (jani_isnan(value)) {
        sink_string(target, "nan");
        return;
    }

    if (value < 0.0) {
        sink_put(target, '-');
        value = -value;
    }

    if (jani_isinf(value) || (value >= DOUBLE_MAX_WHOLE)) {
        sink_string(target, "inf");
        return;
    }

    whole = (unsigned long long)value;
    fraction = value - (double)whole;

    sink_unsigned(target, whole, 10u, 0);
    sink_put(target, '.');

    for (index = 0; index < DOUBLE_FRACTION_DIGITS; index++) {
        fraction *= 10.0;
        digit = (unsigned)fraction;
        if (digit > 9u) {
            digit = 9u;
        }
        sink_put(target, (char)('0' + (char)digit));
        fraction -= (double)digit;
    }
}

int jani_vsnprintf(
    char *buffer,
    size_t size,
    const char *format,
    va_list args
) {
    struct sink target;
    size_t index;
    unsigned longs;

    target.buffer = buffer;
    target.size = size;
    target.written = 0;

    index = 0;
    while (format[index] != '\0') {
        if (format[index] != '%') {
            sink_put(&target, format[index]);
            index++;
            continue;
        }

        if (format[index + 1u] == '\0') {
            sink_put(&target, '%');
            index++;
            continue;
        }

        index++;
        longs = 0;
        while ((format[index] == 'l') || (format[index] == 'z')) {
            longs++;
            index++;
        }

        switch (format[index]) {
            case '%':
                sink_put(&target, '%');
                break;
            case 'c':
                sink_put(&target, (char)va_arg(args, int));
                break;
            case 's': {
                const char *text = va_arg(args, const char *);

                sink_string(&target, (text == 0) ? "(null)" : text);
                break;
            }
            case 'd':
            case 'i':
                if (longs == 0) {
                    sink_signed(&target, (long long)va_arg(args, int));
                } else if (longs == 1u) {
                    sink_signed(&target, (long long)va_arg(args, long));
                } else {
                    sink_signed(&target, va_arg(args, long long));
                }
                break;
            case 'u':
                if (longs == 0) {
                    sink_unsigned(&target,
                                  (unsigned long long)va_arg(args, unsigned),
                                  10u, 0);
                } else if (longs == 1u) {
                    sink_unsigned(
                        &target,
                        (unsigned long long)va_arg(args, unsigned long), 10u, 0
                    );
                } else {
                    sink_unsigned(&target,
                                  va_arg(args, unsigned long long), 10u, 0);
                }
                break;
            case 'x':
            case 'X':
                if (longs == 0) {
                    sink_unsigned(&target,
                                  (unsigned long long)va_arg(args, unsigned),
                                  16u, format[index] == 'X');
                } else if (longs == 1u) {
                    sink_unsigned(
                        &target,
                        (unsigned long long)va_arg(args, unsigned long), 16u,
                        format[index] == 'X'
                    );
                } else {
                    sink_unsigned(&target, va_arg(args, unsigned long long),
                                  16u, format[index] == 'X');
                }
                break;
            case 'p':
                sink_string(&target, "0x");
                sink_unsigned(
                    &target,
                    (unsigned long long)(uintptr_t)va_arg(args, void *), 16u, 0
                );
                break;
            case 'f':
            case 'g':
            case 'e':
                sink_double(&target, va_arg(args, double));
                break;
            default:
                sink_put(&target, '%');
                while (longs > 0) {
                    sink_put(&target, 'l');
                    longs--;
                }
                sink_put(&target, format[index]);
                break;
        }

        index++;
    }

    if (size > 0) {
        if (target.written < size) {
            buffer[target.written] = '\0';
        } else {
            buffer[size - 1u] = '\0';
        }
    }

    return (int)target.written;
}

int jani_snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    int written;

    va_start(args, format);
    written = jani_vsnprintf(buffer, size, format, args);
    va_end(args);

    return written;
}

int jani_vprintf(const char *format, va_list args) {
    char line[512];
    int written;

    written = jani_vsnprintf(line, sizeof(line), format, args);

#ifdef JANI_HOSTED
    fputs(line, stdout);
#else
    kputs(line);
#endif

    return written;
}

int jani_printf(const char *format, ...) {
    va_list args;
    int written;

    va_start(args, format);
    written = jani_vprintf(format, args);
    va_end(args);

    return written;
}

int jani_putchar(int character) {
    char text[2];

    text[0] = (char)character;
    text[1] = '\0';

#ifdef JANI_HOSTED
    fputs(text, stdout);
#else
    kputs(text);
#endif

    return character;
}

int jani_puts(const char *text) {
#ifdef JANI_HOSTED
    fputs(text, stdout);
    fputs("\n", stdout);
#else
    kputs(text);
    kputs("\n");
#endif

    return 0;
}
