#include <stdint.h>
#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/wasm/instance_state.h"
#include "check.h"

#define MEMORY_BYTES 256u
#define BUFFER_BYTES 1024u

unsigned long checks_passed;

static struct component source;
static struct component restored;
static uint8_t source_memory[MEMORY_BYTES];
static uint8_t restored_memory[MEMORY_BYTES];
static uint8_t buffer[BUFFER_BYTES];

static void fill_memory(uint8_t *memory, size_t count, uint8_t seed) {
    size_t index;

    for (index = 0; index < count; index++) {
        memory[index] = (uint8_t)(seed + (uint8_t)(index * 3u));
    }
}

static void build_source(uint32_t mailbox_used) {
    uint32_t index;

    memset(&source, 0, sizeof(source));
    source.logical_time = 4242;
    source.timer_deadline = 4243;
    source.timer_armed = 1;
    source.mailbox_used = mailbox_used == 0 ? 0 : mailbox_used + 8;

    for (index = 0; index < mailbox_used; index++) {
        source.mailbox[8 + index] = (uint8_t)(0xC0 + index);
    }
    if (mailbox_used != 0) {
        uint32_t absent = UINT32_MAX;
        memcpy(source.mailbox, &mailbox_used, 4);
        memcpy(source.mailbox + 4, &absent, 4);
    }

    fill_memory(source_memory, MEMORY_BYTES, 0x11);
}

static size_t serialize_source(void) {
    size_t written;

    written = 0;
    CHECK(instance_state_serialize(&source, source_memory, MEMORY_BYTES,
                                   buffer, sizeof(buffer), &written) == 1);
    CHECK(written == instance_state_size(MEMORY_BYTES, source.mailbox_used));
    return written;
}

static void test_round_trip(void) {
    size_t written;
    size_t index;

    build_source(8);
    written = serialize_source();

    memset(&restored, 0, sizeof(restored));
    memset(restored_memory, 0xFF, sizeof(restored_memory));

    CHECK(instance_state_deserialize(&restored, restored_memory, MEMORY_BYTES,
                                     buffer, written) == 1);

    CHECK(restored.logical_time == source.logical_time);
    CHECK(restored.timer_deadline == source.timer_deadline);
    CHECK(restored.timer_armed == source.timer_armed);
    CHECK(restored.mailbox_used == source.mailbox_used);

    for (index = 0; index < source.mailbox_used; index++) {
        CHECK(restored.mailbox[index] == source.mailbox[index]);
    }
    for (index = 0; index < MEMORY_BYTES; index++) {
        CHECK(restored_memory[index] == source_memory[index]);
    }
}

static void test_empty_mailbox_round_trip(void) {
    size_t written;

    build_source(0);
    written = serialize_source();

    memset(&restored, 0, sizeof(restored));
    CHECK(instance_state_deserialize(&restored, restored_memory, MEMORY_BYTES,
                                     buffer, written) == 1);
    CHECK(restored.mailbox_used == 0);
    CHECK(restored.logical_time == source.logical_time);
}

static void test_serialize_rejects_small_capacity(void) {
    size_t written;

    build_source(4);
    written = 0xFFFF;

    CHECK(instance_state_serialize(&source, source_memory, MEMORY_BYTES,
                                   buffer,
                                   instance_state_size(MEMORY_BYTES, 4) - 1,
                                   &written) == 0);
}

static void test_serialize_rejects_oversized_mailbox(void) {
    size_t written;

    build_source(0);
    source.mailbox_used = COMPONENT_MAILBOX_BYTES + 1;
    written = 0;

    CHECK(instance_state_serialize(&source, source_memory, MEMORY_BYTES,
                                   buffer, sizeof(buffer), &written) == 0);
}

static void test_deserialize_rejects_larger_saved_memory(void) {
    size_t written;

    build_source(0);
    written = serialize_source();

    CHECK(instance_state_deserialize(&restored, restored_memory,
                                     MEMORY_BYTES - 1, buffer, written) == 0);
}

static void test_deserialize_zeroes_the_tail(void) {
    size_t written;
    size_t index;
    uint8_t wide_memory[MEMORY_BYTES * 2];

    build_source(0);
    written = serialize_source();

    memset(wide_memory, 0xAA, sizeof(wide_memory));
    CHECK(instance_state_deserialize(&restored, wide_memory,
                                     sizeof(wide_memory), buffer,
                                     written) == 1);

    for (index = 0; index < MEMORY_BYTES; index++) {
        CHECK(wide_memory[index] == source_memory[index]);
    }
    for (index = MEMORY_BYTES; index < sizeof(wide_memory); index++) {
        CHECK(wide_memory[index] == 0x00);
    }
}

static void test_validate_rejects_corruption(void) {
    size_t written;
    size_t memory_offset;
    size_t memory_size;
    uint8_t saved;

    build_source(4);
    written = serialize_source();

    CHECK(instance_state_header_validate(buffer, written, &memory_offset,
                                         &memory_size) == 1);
    CHECK(memory_size == MEMORY_BYTES);
    CHECK(memory_offset == INSTANCE_STATE_HEADER_SIZE + 12u);

    saved = buffer[0];
    buffer[0] = (uint8_t)(saved ^ 0xFF);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 0);
    buffer[0] = saved;

    saved = buffer[INSTANCE_STATE_HEADER_SIZE + 2u];
    buffer[INSTANCE_STATE_HEADER_SIZE + 2u] = (uint8_t)(saved ^ 0xFF);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 0);
    buffer[INSTANCE_STATE_HEADER_SIZE + 2u] = saved;

    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 1);
    CHECK(instance_state_header_validate(buffer, written - 1, NULL, NULL) == 0);
    CHECK(instance_state_header_validate(buffer, 0, NULL, NULL) == 0);
    CHECK(instance_state_header_validate(NULL, written, NULL, NULL) == 0);
}

static void test_validate_rejects_bad_header_fields(void) {
    struct instance_state_header header;
    size_t written;

    build_source(4);
    written = serialize_source();

    memcpy(&header, buffer, INSTANCE_STATE_HEADER_SIZE);

    header.format_version += 1;
    memcpy(buffer, &header, INSTANCE_STATE_HEADER_SIZE);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 0);
    header.format_version -= 1;

    header.timer_armed = 2;
    memcpy(buffer, &header, INSTANCE_STATE_HEADER_SIZE);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 0);
    header.timer_armed = 1;

    header._reserved = 1;
    memcpy(buffer, &header, INSTANCE_STATE_HEADER_SIZE);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 0);
    header._reserved = 0;

    header.mailbox_used = COMPONENT_MAILBOX_BYTES + 1;
    memcpy(buffer, &header, INSTANCE_STATE_HEADER_SIZE);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 0);
    header.mailbox_used = 12;

    memcpy(buffer, &header, INSTANCE_STATE_HEADER_SIZE);
    CHECK(instance_state_header_validate(buffer, written, NULL, NULL) == 1);
}

static void test_deserialize_rejects_corrupt_payload(void) {
    size_t written;

    build_source(4);
    written = serialize_source();

    buffer[INSTANCE_STATE_HEADER_SIZE] ^= 0xFF;
    CHECK(instance_state_deserialize(&restored, restored_memory, MEMORY_BYTES,
                                     buffer, written) == 0);
}

int main(void) {
    checks_passed = 0;

    test_round_trip();
    test_empty_mailbox_round_trip();
    test_serialize_rejects_small_capacity();
    test_serialize_rejects_oversized_mailbox();
    test_deserialize_rejects_larger_saved_memory();
    test_deserialize_zeroes_the_tail();
    test_validate_rejects_corruption();
    test_validate_rejects_bad_header_fields();
    test_deserialize_rejects_corrupt_payload();

    printf("test_component_state: %lu checks passed\n", checks_passed);
    return 0;
}
