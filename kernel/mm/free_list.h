#ifndef JANI_KERNEL_MM_FREE_LIST_H
#define JANI_KERNEL_MM_FREE_LIST_H

#include <stddef.h>

void heap_free_list_reset(void);
int heap_free_list_add(void *memory, size_t size);
void *heap_free_list_take(size_t minimum_size, size_t *actual_size);

#endif