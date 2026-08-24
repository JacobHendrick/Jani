#include <stdint.h>
#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/wasm/component.h"
#include "check.h"

unsigned long checks_passed;

int jani_wasm_instance_create(
    const uint8_t *bytes,
    size_t length,
    void **module_out,
    void **instance_out,
    void **exec_env_out,
    void **owned_bytes_out
) {
    (void)bytes;
    (void)length;
    (void)module_out;
    (void)instance_out;
    (void)exec_env_out;
    (void)owned_bytes_out;
    return 0;
}

void jani_wasm_instance_destroy(
    void *module,
    void *instance,
    void *exec_env,
    void *owned_bytes
) {
    (void)module;
    (void)instance;
    (void)exec_env;
    (void)owned_bytes;
}

int jani_wasm_instance_memory(
    void *instance,
    uint8_t **base_out,
    size_t *size_out
) {
    (void)instance;
    (void)base_out;
    (void)size_out;
    return 0;
}

int jani_wasm_instance_memory_grow(void *instance, size_t required_bytes) {
    (void)instance;
    (void)required_bytes;
    return 0;
}

int jani_wasm_instance_call(void *instance, void *exec_env, const char *name) {
    (void)instance;
    (void)exec_env;
    (void)name;
    return 0;
}

void jani_wasm_set_current_component(struct component *component) {
    (void)component;
}

void *kmalloc(size_t size) {
    (void)size;
    return NULL;
}

void kfree(void *memory) {
    (void)memory;
}

static struct component component;

static void reset(void) {
    memset(&component, 0, sizeof(component));
    capability_table_init(&component.capability_table);
}

static void test_push_pop_round_trip(void) {
    uint8_t out[16];
    uint32_t length;
    int32_t capability;

    reset();

    CHECK(component_mailbox_push(&component, (const uint8_t *)"ping", 4,
                                 -1) == 1);
    CHECK(component.mailbox_used == 12);

    length = 0;
    capability = 0;
    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 1);
    CHECK(length == 4);
    CHECK(capability == -1);
    CHECK(out[0] == 'p');
    CHECK(out[3] == 'g');
    CHECK(component.mailbox_used == 0);
}

static void test_pop_on_empty_fails(void) {
    uint8_t out[16];
    uint32_t length;
    int32_t capability;

    reset();
    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 0);
}

static void test_fifo_order(void) {
    uint8_t out[16];
    uint32_t length;
    int32_t capability;

    reset();

    CHECK(component_mailbox_push(&component, (const uint8_t *)"aa", 2, 1) == 1);
    CHECK(component_mailbox_push(&component, (const uint8_t *)"bbb", 3, 2) == 1);
    CHECK(component_mailbox_push(&component, (const uint8_t *)"c", 1, 3) == 1);

    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 1);
    CHECK((length == 2) && (out[0] == 'a') && (capability == 1));

    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 1);
    CHECK((length == 3) && (out[0] == 'b') && (capability == 2));

    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 1);
    CHECK((length == 1) && (out[0] == 'c') && (capability == 3));

    CHECK(component.mailbox_used == 0);
    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 0);
}

static void test_zero_length_message(void) {
    uint8_t out[16];
    uint32_t length;
    int32_t capability;

    reset();

    CHECK(component_mailbox_push(&component, NULL, 0, -1) == 1);
    CHECK(component.mailbox_used == 8);
    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 1);
    CHECK(length == 0);
    CHECK(component.mailbox_used == 0);
}

static void test_push_respects_capacity(void) {
    uint8_t payload[COMPONENT_MAILBOX_BYTES];
    uint32_t room;

    reset();
    memset(payload, 0x5A, sizeof(payload));

    room = COMPONENT_MAILBOX_BYTES - 8u;
    CHECK(component_mailbox_push(&component, payload, room, -1) == 1);
    CHECK(component.mailbox_used == COMPONENT_MAILBOX_BYTES);

    CHECK(component_mailbox_push(&component, payload, 1, -1) == 0);
    CHECK(component.mailbox_used == COMPONENT_MAILBOX_BYTES);
}

static void test_push_rejects_oversized(void) {
    uint8_t payload[COMPONENT_MAILBOX_BYTES];

    reset();
    memset(payload, 0x5A, sizeof(payload));

    CHECK(component_mailbox_push(&component, payload,
                                 COMPONENT_MAILBOX_BYTES, -1) == 0);
    CHECK(component.mailbox_used == 0);
}

static void test_push_rejects_wrapped_lengths(void) {
    uint8_t byte;

    reset();
    byte = 0;

    CHECK(component_mailbox_push(&component, &byte, UINT32_MAX, -1) == 0);
    CHECK(component.mailbox_used == 0);

    component.mailbox_used = COMPONENT_MAILBOX_BYTES + 1u;
    CHECK(component_mailbox_push(&component, &byte, 1, -1) == 0);
    CHECK(component.mailbox_used == COMPONENT_MAILBOX_BYTES + 1u);
}

static void test_pop_rejects_small_capacity(void) {
    uint8_t out[2];
    uint32_t length;
    int32_t capability;

    reset();
    CHECK(component_mailbox_push(&component, (const uint8_t *)"hello", 5,
                                 -1) == 1);
    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 0);
    CHECK(component.mailbox_used == 13);
}

static void test_pop_rejects_corrupt_frames(void) {
    uint8_t out[16];
    uint32_t header[2];
    uint32_t length;
    int32_t capability;

    reset();
    header[0] = UINT32_MAX;
    header[1] = 0;
    memcpy(component.mailbox, header, sizeof(header));
    component.mailbox_used = (uint32_t)sizeof(header);

    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 0);
    CHECK(component.mailbox_used == sizeof(header));

    component.mailbox_used = COMPONENT_MAILBOX_BYTES + 1u;
    CHECK(component_mailbox_pop(&component, out, sizeof(out), &length,
                                &capability) == 0);
}

static void test_pop_requires_output_buffer(void) {
    uint32_t length;
    int32_t capability;

    reset();
    CHECK(component_mailbox_push(&component, (const uint8_t *)"x", 1,
                                 -1) == 1);
    CHECK(component_mailbox_pop(&component, NULL, 1, &length,
                                &capability) == 0);
    CHECK(component.mailbox_used == 9);
}

static void test_capability_slots(void) {
    struct object_id first;
    struct object_id second;
    uint32_t slot;

    reset();
    first = component_make_id(3, 1);
    second = component_make_id(3, 2);

    CHECK(component_capability_find(&component, first, &slot) == 0);

    CHECK(component_capability_insert(&component, first,
                                      COMPONENT_RIGHTS_READ, 0, &slot) == 1);
    CHECK(slot == 0);
    CHECK(capability_table_generation(&component.capability_table, slot) == 1);
    CHECK(component.capability_count == 1);
    CHECK(component.capabilities_dirty == 1);

    CHECK(component_capability_insert(&component, second,
                                      COMPONENT_RIGHTS_WRITE, 0, &slot) == 1);
    CHECK(slot == 1);

    CHECK(component_capability_find(&component, first, &slot) == 1);
    CHECK(slot == 0);

    CHECK(component_capability_insert(&component, first,
                                      COMPONENT_RIGHTS_WRITE, 0, &slot) == 1);
    CHECK(slot == 0);
    CHECK(component.capability_count == 2);
    CHECK(component.capability_table.slots[0].rights ==
          (COMPONENT_RIGHTS_READ | COMPONENT_RIGHTS_WRITE));

    CHECK(component_capability_insert(&component, component_make_id(0, 0),
                                      COMPONENT_RIGHTS_READ, 0, &slot) == 0);
}

static void test_dropped_slot_is_reused(void) {
    struct object_id first;
    struct object_id second;
    struct object_id third;
    uint32_t slot;

    reset();
    first = component_make_id(3, 1);
    second = component_make_id(3, 2);
    third = component_make_id(3, 3);

    CHECK(component_capability_insert(&component, first, 1, 0, &slot) == 1);
    CHECK(component_capability_insert(&component, second, 1, 0, &slot) == 1);
    CHECK(component.capability_table.generations[0] == 1);

    memset(&component.capability_table.slots[0], 0,
           sizeof(component.capability_table.slots[0]));
    component.capability_table.parents[0] = COMPONENT_CAP_PARENT_NONE;

    CHECK(component_capability_find(&component, first, &slot) == 0);
    CHECK(component_capability_insert(&component, third, 1, 0, &slot) == 1);
    CHECK(slot == 0);
    CHECK(capability_table_generation(&component.capability_table, slot) == 2);
    CHECK(component.capability_count == 2);
}

static void test_capability_table_is_bounded(void) {
    uint32_t slot;
    uint32_t index;

    reset();

    for (index = 0; index < COMPONENT_CAP_SLOTS; index++) {
        CHECK(component_capability_insert(&component,
                                          component_make_id(3, index + 1u),
                                          1, 0, &slot) == 1);
    }

    CHECK(component.capability_count == COMPONENT_CAP_SLOTS);
    CHECK(component_capability_insert(&component,
                                      component_make_id(3, 999), 1, 0,
                                      &slot) == 0);
}

static void test_component_capability_derivation(void) {
    uint32_t root_slot;
    uint32_t child_slot;
    uint32_t grandchild_slot;

    reset();

    CHECK(component_capability_insert(
              &component,
              component_make_id(3, 42),
              COMPONENT_RIGHTS_READ | COMPONENT_RIGHTS_WRITE |
                  COMPONENT_RIGHTS_GRANT,
              7,
              &root_slot
          ) == 1);
    component.capabilities_dirty = 0;

    CHECK(component_capability_derive(
              &component,
              root_slot,
              COMPONENT_RIGHTS_READ | COMPONENT_RIGHTS_GRANT,
              8,
              &child_slot
          ) == 1);
    CHECK(child_slot == 1);
    CHECK(component.capability_count == 2);
    CHECK(component.capability_table.parents[child_slot] == root_slot);
    CHECK(capability_table_generation(
              &component.capability_table, child_slot
          ) == 1);
    CHECK(component.capability_table.slots[child_slot].badge == 8);
    CHECK(component.capabilities_dirty == 1);

    CHECK(component_capability_derive(
              &component,
              child_slot,
              COMPONENT_RIGHTS_READ,
              9,
              &grandchild_slot
          ) == 1);
    CHECK(grandchild_slot == 2);
    CHECK(component_capability_derive(
              &component,
              grandchild_slot,
              COMPONENT_RIGHTS_READ,
              10,
              &root_slot
          ) == 0);

    component.capabilities_dirty = 0;
    CHECK(component_capability_revoke(&component, child_slot) == 1);
    CHECK(capability_table_get(&component.capability_table, child_slot) ==
          NULL);
    CHECK(capability_table_get(&component.capability_table,
                               grandchild_slot) == NULL);
    CHECK(capability_table_get(&component.capability_table, 0) != NULL);
    CHECK(component.capability_count == 1);
    CHECK(component.capabilities_dirty == 1);
    CHECK(component_capability_revoke(&component, child_slot) == 0);
}

int main(void) {
    checks_passed = 0;

    test_push_pop_round_trip();
    test_pop_on_empty_fails();
    test_fifo_order();
    test_zero_length_message();
    test_push_respects_capacity();
    test_push_rejects_oversized();
    test_push_rejects_wrapped_lengths();
    test_pop_rejects_small_capacity();
    test_pop_rejects_corrupt_frames();
    test_pop_requires_output_buffer();
    test_capability_slots();
    test_dropped_slot_is_reused();
    test_capability_table_is_bounded();
    test_component_capability_derivation();

    printf("test_component_mailbox: %lu checks passed\n", checks_passed);
    return 0;
}
