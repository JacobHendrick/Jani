#include <stdint.h>
#include <stddef.h>

#include "../../kernel/wasm/shim/jani_libc.h"

#define FUZZ_FORMAT_CAPACITY 128u
#define FUZZ_BUFFER_CAPACITY 64u
#define FUZZ_GUARD_BYTE 0xA5u

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char format[FUZZ_FORMAT_CAPACITY];
    char buffer[FUZZ_BUFFER_CAPACITY + 1u];
    size_t requested;
    size_t format_length;
    size_t index;
    int written;

    if (size < 2u) {
        return 0;
    }

    requested = (size_t)data[0] % (FUZZ_BUFFER_CAPACITY + 1u);
    data++;
    size--;

    format_length = (size < (FUZZ_FORMAT_CAPACITY - 1u))
                        ? size
                        : (FUZZ_FORMAT_CAPACITY - 1u);
    for (index = 0; index < format_length; index++) {
        format[index] = (char)data[index];
    }
    format[format_length] = '\0';

    for (index = 0; index <= FUZZ_BUFFER_CAPACITY; index++) {
        buffer[index] = (char)FUZZ_GUARD_BYTE;
    }

    written = jani_snprintf(buffer, requested, "%s", format);

    if (written < 0) {
        __builtin_trap();
    }

    if (buffer[FUZZ_BUFFER_CAPACITY] != (char)FUZZ_GUARD_BYTE) {
        __builtin_trap();
    }

    if (requested > 0) {
        size_t terminator;

        terminator = ((size_t)written < requested) ? (size_t)written
                                                   : (requested - 1u);
        if (buffer[terminator] != '\0') {
            __builtin_trap();
        }
        for (index = requested; index <= FUZZ_BUFFER_CAPACITY; index++) {
            if (buffer[index] != (char)FUZZ_GUARD_BYTE) {
                __builtin_trap();
            }
        }
    } else {
        for (index = 0; index <= FUZZ_BUFFER_CAPACITY; index++) {
            if (buffer[index] != (char)FUZZ_GUARD_BYTE) {
                __builtin_trap();
            }
        }
    }

    return 0;
}
