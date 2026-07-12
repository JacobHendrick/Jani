#include <stddef.h>

#include "string.h"

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *dest_bytes = (unsigned char *)dest;
    const unsigned char *src_bytes = (const unsigned char *)src;
    size_t i;

    for (i = 0; i < n; i++) {
        dest_bytes[i] = src_bytes[i];
    }

    return dest;
}

void *memset(void *dest, int value, size_t n) {
    unsigned char *dest_bytes = (unsigned char *)dest;
    unsigned char byte_value = (unsigned char)value;
    size_t i;

    for (i = 0; i < n; i++) {
        dest_bytes[i] = byte_value;
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *dest_bytes = (unsigned char *)dest;
    const unsigned char *src_bytes = (const unsigned char *)src;
    size_t i;

    if (dest_bytes == src_bytes) {
        return dest;
    }


    if (dest_bytes < src_bytes) {
        for (i = 0; i < n; i++) {
            dest_bytes[i] = src_bytes[i];
        }
    } else {
        for (i = n; i > 0; i--) {
            dest_bytes[i - 1] = src_bytes[i - 1];
        }
    }

    return dest;

}

size_t strlen(const char *str) {
    size_t length = 0;

    while (str[length] != '\0') {
        length++;
    }

    return length;
}