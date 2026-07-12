#ifndef JANI_KERNEL_LIB_STRING_H
#define JANI_KERNEL_LIB_STRING_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
void *memmove(void *dest, const void *src, size_t n);
size_t strlen(const char *str);

#endif // JANI_KERNEL_LIB_STRING_H