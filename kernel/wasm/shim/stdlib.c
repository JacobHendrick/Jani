#include "jani_libc.h"

#ifdef JANI_HOSTED
#include <stdlib.h>
#else
#include "../../lib/string.h"
#include "../../mm/heap.h"
#endif

#define STRTOL_MAX_BASE 36

void *jani_malloc(size_t size) {
#ifdef JANI_HOSTED
    return malloc(size);
#else
    return kmalloc(size);
#endif
}

void jani_free(void *memory) {
#ifdef JANI_HOSTED
    free(memory);
#else
    kfree(memory);
#endif
}

void *jani_realloc(void *memory, size_t size) {
#ifdef JANI_HOSTED
    return realloc(memory, size);
#else
    return krealloc(memory, size);
#endif
}

void *jani_calloc(size_t count, size_t size) {
    void *memory;
    size_t total;

    if ((count != 0) && (size > (SIZE_MAX / count))) {
        return 0;
    }

    total = count * size;
    memory = jani_malloc(total);
    if (memory == 0) {
        return 0;
    }

#ifdef JANI_HOSTED
    {
        unsigned char *bytes = memory;
        size_t index;

        for (index = 0; index < total; index++) {
            bytes[index] = 0;
        }
    }
#else
    memset(memory, 0, total);
#endif

    return memory;
}

int jani_abs(int value) {
    return (value < 0) ? -value : value;
}

static int digit_value(char character) {
    if ((character >= '0') && (character <= '9')) {
        return character - '0';
    }
    if ((character >= 'a') && (character <= 'z')) {
        return (character - 'a') + 10;
    }
    if ((character >= 'A') && (character <= 'Z')) {
        return (character - 'A') + 10;
    }

    return -1;
}

static unsigned long parse_unsigned(
    const char *text,
    char **end,
    int base,
    int *negative_out
) {
    unsigned long accumulated;
    size_t index;
    size_t digits;
    int value;

    index = 0;
    while ((text[index] == ' ') || (text[index] == '\t') ||
           (text[index] == '\n') || (text[index] == '\r')) {
        index++;
    }

    *negative_out = 0;
    if (text[index] == '-') {
        *negative_out = 1;
        index++;
    } else if (text[index] == '+') {
        index++;
    }

    if (((base == 0) || (base == 16)) && (text[index] == '0') &&
        ((text[index + 1u] == 'x') || (text[index + 1u] == 'X'))) {
        index += 2u;
        base = 16;
    } else if (base == 0) {
        base = (text[index] == '0') ? 8 : 10;
    }

    if ((base < 2) || (base > STRTOL_MAX_BASE)) {
        if (end != 0) {
            *end = (char *)text;
        }
        return 0;
    }

    accumulated = 0;
    digits = 0;
    for (;;) {
        value = digit_value(text[index]);
        if ((value < 0) || (value >= base)) {
            break;
        }
        accumulated = (accumulated * (unsigned long)base) +
                      (unsigned long)value;
        digits++;
        index++;
    }

    if (end != 0) {
        *end = (digits == 0) ? (char *)text : (char *)&text[index];
    }

    return accumulated;
}

long jani_strtol(const char *text, char **end, int base) {
    unsigned long magnitude;
    int negative;

    magnitude = parse_unsigned(text, end, base, &negative);

    return negative ? -(long)magnitude : (long)magnitude;
}

unsigned long jani_strtoul(const char *text, char **end, int base) {
    unsigned long magnitude;
    int negative;

    magnitude = parse_unsigned(text, end, base, &negative);

    return negative ? (unsigned long)(-(long)magnitude) : magnitude;
}

int jani_errno;
