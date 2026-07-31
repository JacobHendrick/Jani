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

long jani_labs(long value) {
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

#define JANI_QSORT_SMALL 16

static void swap_elements(unsigned char *left, unsigned char *right,
                          size_t size) {
    size_t index;

    for (index = 0; index < size; index++) {
        unsigned char temporary;

        temporary = left[index];
        left[index] = right[index];
        right[index] = temporary;
    }
}

static void insertion_sort(unsigned char *base, size_t count, size_t size,
                           jani_compare_fn compare) {
    size_t index;

    for (index = 1; index < count; index++) {
        size_t cursor;

        for (cursor = index; cursor > 0; cursor--) {
            if (compare(base + ((cursor - 1) * size),
                        base + (cursor * size)) <= 0) {
                break;
            }
            swap_elements(base + ((cursor - 1) * size),
                          base + (cursor * size), size);
        }
    }
}

static void sift_down(unsigned char *base, size_t size, size_t root,
                      size_t count, jani_compare_fn compare) {
    while (((root * 2) + 1) < count) {
        size_t child;

        child = (root * 2) + 1;

        if (((child + 1) < count) &&
            (compare(base + (child * size),
                     base + ((child + 1) * size)) < 0)) {
            child = child + 1;
        }

        if (compare(base + (root * size), base + (child * size)) >= 0) {
            return;
        }

        swap_elements(base + (root * size), base + (child * size), size);
        root = child;
    }
}

static void heap_sort(unsigned char *base, size_t count, size_t size,
                      jani_compare_fn compare) {
    size_t index;

    if (count < 2) {
        return;
    }

    index = count / 2;
    while (index > 0) {
        index = index - 1;
        sift_down(base, size, index, count, compare);
    }

    index = count;
    while (index > 1) {
        index = index - 1;
        swap_elements(base, base + (index * size), size);
        sift_down(base, size, 0, index, compare);
    }
}

static void order_pair(unsigned char *left, unsigned char *right, size_t size,
                       jani_compare_fn compare) {
    if (compare(left, right) > 0) {
        swap_elements(left, right, size);
    }
}

static size_t partition_around_median(unsigned char *base, size_t count,
                                      size_t size, jani_compare_fn compare) {
    unsigned char *pivot;
    size_t middle;
    size_t last;
    size_t boundary;
    size_t index;

    middle = count / 2;
    last = count - 1;

    order_pair(base, base + (middle * size), size, compare);
    order_pair(base, base + (last * size), size, compare);
    order_pair(base + (middle * size), base + (last * size), size, compare);
    swap_elements(base + (middle * size), base + (last * size), size);

    pivot = base + (last * size);
    boundary = 0;

    for (index = 0; index < last; index++) {
        if (compare(base + (index * size), pivot) < 0) {
            swap_elements(base + (boundary * size), base + (index * size),
                          size);
            boundary = boundary + 1;
        }
    }

    swap_elements(base + (boundary * size), pivot, size);

    return boundary;
}

static size_t depth_limit_for(size_t count) {
    size_t limit;
    size_t value;

    limit = 0;
    value = count;

    while (value > 1) {
        value = value / 2;
        limit = limit + 1;
    }

    return limit * 2;
}

static void introsort(unsigned char *base, size_t count, size_t size,
                      jani_compare_fn compare, size_t depth_limit) {
    while (count > JANI_QSORT_SMALL) {
        size_t split;
        size_t left_count;
        size_t right_count;

        if (depth_limit == 0) {
            heap_sort(base, count, size, compare);
            return;
        }

        depth_limit = depth_limit - 1;

        split = partition_around_median(base, count, size, compare);
        left_count = split;
        right_count = count - split - 1;

        if (left_count < right_count) {
            introsort(base, left_count, size, compare, depth_limit);
            base = base + ((split + 1) * size);
            count = right_count;
        } else {
            introsort(base + ((split + 1) * size), right_count, size, compare,
                      depth_limit);
            count = left_count;
        }
    }

    insertion_sort(base, count, size, compare);
}

void jani_qsort(void *base, size_t count, size_t size,
                jani_compare_fn compare) {
    if ((base == 0) || (count < 2) || (size == 0)) {
        return;
    }

    introsort((unsigned char *)base, count, size, compare,
              depth_limit_for(count));
}

void *jani_bsearch(const void *key, const void *base, size_t count,
                   size_t size, jani_compare_fn compare) {
    const unsigned char *elements;
    size_t low;
    size_t high;

    elements = (const unsigned char *)base;
    low = 0;
    high = count;

    while (low < high) {
        size_t middle;
        int order;

        middle = low + ((high - low) / 2);
        order = compare(key, elements + (middle * size));

        if (order < 0) {
            high = middle;
        } else if (order > 0) {
            low = middle + 1;
        } else {
            return (void *)(elements + (middle * size));
        }
    }

    return 0;
}

int jani_errno;
