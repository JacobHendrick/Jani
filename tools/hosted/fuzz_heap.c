#include <stddef.h>
#include <stdint.h>

#include "../../kernel/mm/heap.h"
#include "hosted_heap.h"

#define FUZZ_SLOT_COUNT 64
#define FUZZ_MAX_ALLOCATION_SIZE 4096

struct fuzz_slot {
    uint8_t *memory;
    size_t size;
    uint8_t pattern;
};

static void verify_slot(const struct fuzz_slot *slot) {
    size_t index;

    if (slot->memory == NULL) {
        return;
    }

    for (index = 0; index < slot->size; index++) {
        if (slot->memory[index] != slot->pattern) {
            __builtin_trap();
        }
    }
}

static void fill_slot(struct fuzz_slot *slot, uint8_t pattern) {
    size_t index;

    slot->pattern = pattern;

    for (index = 0; index < slot->size; index++) {
        slot->memory[index] = pattern;
    }
}

static size_t decode_size(const uint8_t *data, size_t cursor) {
    size_t encoded_size;

    encoded_size = ((size_t)data[cursor] << 8) | data[cursor + 1];
    return encoded_size % (FUZZ_MAX_ALLOCATION_SIZE + 1);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    struct fuzz_slot slots[FUZZ_SLOT_COUNT] = { 0 };
    size_t cursor = 0;
    size_t slot_index;

    hosted_heap_reset();
    kheap_init(hosted_heap_arena_base(), hosted_heap_arena_size());

    while ((size - cursor) >= 5) {
        struct fuzz_slot *slot;
        uint8_t operation = data[cursor++] % 3;
        size_t requested_size;
        uint8_t pattern;

        slot_index = data[cursor++] % FUZZ_SLOT_COUNT;
        requested_size = decode_size(data, cursor);
        cursor += 2;
        pattern = data[cursor++];
        slot = &slots[slot_index];

        verify_slot(slot);

        if (operation == 0) {
            if ((slot->memory == NULL) && (requested_size != 0)) {
                slot->memory = kmalloc(requested_size);
                if (slot->memory != NULL) {
                    slot->size = requested_size;
                    fill_slot(slot, pattern);
                }
            }
        } else if (operation == 1) {
            kfree(slot->memory);
            slot->memory = NULL;
            slot->size = 0;
        } else {
            uint8_t *old_memory = slot->memory;
            size_t old_size = slot->size;
            uint8_t *resized = krealloc(slot->memory, requested_size);

            if (requested_size == 0) {
                if (resized != NULL) {
                    __builtin_trap();
                }
                slot->memory = NULL;
                slot->size = 0;
            } else if (resized == NULL) {
                slot->memory = old_memory;
                slot->size = old_size;
                verify_slot(slot);
            } else {
                size_t preserved_size = old_size;
                size_t index;

                if (preserved_size > requested_size) {
                    preserved_size = requested_size;
                }

                for (index = 0; index < preserved_size; index++) {
                    if (resized[index] != slot->pattern) {
                        __builtin_trap();
                    }
                }

                slot->memory = resized;
                slot->size = requested_size;
                fill_slot(slot, pattern);
            }
        }
    }

    for (slot_index = 0; slot_index < FUZZ_SLOT_COUNT; slot_index++) {
        verify_slot(&slots[slot_index]);
        kfree(slots[slot_index].memory);
    }

    return 0;
}
