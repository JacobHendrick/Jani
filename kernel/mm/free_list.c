#include <stddef.h>
#include <stdint.h>

#include "free_list.h"

#define FREE_BLOCK_ALIGNMENT 16
#define FREE_LIST_CLASS_COUNT 9
#define FREE_LIST_SMALLEST_CLASS 32

struct free_block {
    size_t size;
    struct free_block *next;
};

static struct free_block *free_list_heads[FREE_LIST_CLASS_COUNT];

static size_t class_for_size(size_t size) {
    size_t class_index = 0;
    size_t class_limit = FREE_LIST_SMALLEST_CLASS;

    while ((class_index < (FREE_LIST_CLASS_COUNT - 1)) &&
           (size > class_limit)) {
        class_limit *= 2;
        class_index++;
    }

    return class_index;
}

void heap_free_list_reset(void) {
    size_t class_index;

    for (class_index = 0;
         class_index < FREE_LIST_CLASS_COUNT;
         class_index++) {
        free_list_heads[class_index] = NULL;
    }
}

int heap_free_list_add(void *memory, size_t size) {
    struct free_block *block;
    size_t class_index;

    if ((memory == NULL) ||
        (((uintptr_t)memory % FREE_BLOCK_ALIGNMENT) != 0) ||
        (size < sizeof(struct free_block))) {
        return 0;
    }

    class_index = class_for_size(size);
    block = memory;
    block->size = size;
    block->next = free_list_heads[class_index];
    free_list_heads[class_index] = block;

    return 1;
}

void *heap_free_list_take(size_t minimum_size, size_t *actual_size) {
    size_t class_index;

    if ((minimum_size == 0) || (actual_size == NULL)) {
        return NULL;
    }

    class_index = class_for_size(minimum_size);

    for (; class_index < FREE_LIST_CLASS_COUNT; class_index++) {
        struct free_block *previous = NULL;
        struct free_block *current = free_list_heads[class_index];

        while (current != NULL) {
            if (current->size >= minimum_size) {
                if (previous == NULL) {
                    free_list_heads[class_index] = current->next;
                } else {
                    previous->next = current->next;
                }

                *actual_size = current->size;
                return current;
            }

            previous = current;
            current = current->next;
        }
    }

    return NULL;
}
