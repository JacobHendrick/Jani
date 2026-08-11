#include <stdint.h>
#include <stdio.h>

#include "../../kernel/cap/cap_table.h"
#include "check.h"

unsigned long checks_passed;

static struct capability make_capability(uint32_t rights, uint32_t badge) {
    struct capability capability;

    capability.object.high = UINT64_C(3);
    capability.object.low = UINT64_C(42);
    capability.rights = rights;
    capability.badge = badge;
    return capability;
}

static void test_validation(void) {
    struct capability capability;

    CHECK(sizeof(struct capability) == CAPABILITY_SIZE);

    capability = make_capability(CAP_RIGHT_READ, 0);
    CHECK(capability_is_valid(&capability) == 1);
    CHECK(capability_is_valid(NULL) == 0);

    capability.object.high = 0;
    capability.object.low = 0;
    CHECK(capability_is_valid(&capability) == 0);

    capability = make_capability(0, 0);
    CHECK(capability_is_valid(&capability) == 0);

    capability = make_capability(UINT32_C(0x10), 0);
    CHECK(capability_is_valid(&capability) == 0);
}

static void test_permission_checks(void) {
    struct capability capability;

    capability = make_capability(CAP_RIGHT_READ | CAP_RIGHT_WRITE, 0);

    CHECK(capability_allows(&capability, CAP_RIGHT_READ) == 1);
    CHECK(capability_allows(&capability, CAP_RIGHT_WRITE) == 1);
    CHECK(capability_allows(
              &capability,
              CAP_RIGHT_READ | CAP_RIGHT_WRITE
          ) == 1);
    CHECK(capability_allows(&capability, CAP_RIGHT_SEND) == 0);
    CHECK(capability_allows(&capability, 0) == 0);
    CHECK(capability_allows(&capability, UINT32_C(0x10)) == 0);
    CHECK(capability_allows(NULL, CAP_RIGHT_READ) == 0);
}

static void test_derivation(void) {
    struct capability parent;
    struct capability child;

    parent = make_capability(
        CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT,
        7
    );

    CHECK(capability_derive(&parent, CAP_RIGHT_READ, 99, &child) == 1);
    CHECK(object_id_equal(child.object, parent.object) == 1);
    CHECK(child.rights == CAP_RIGHT_READ);
    CHECK(child.badge == 99);
    CHECK(parent.rights ==
          (CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT));
    CHECK(parent.badge == 7);
}

static void test_invalid_derivation(void) {
    struct capability parent;
    struct capability child;

    parent = make_capability(CAP_RIGHT_READ, 0);
    CHECK(capability_derive(&parent, CAP_RIGHT_READ, 0, &child) == 0);

    parent = make_capability(CAP_RIGHT_READ | CAP_RIGHT_GRANT, 0);
    CHECK(capability_derive(&parent, CAP_RIGHT_WRITE, 0, &child) == 0);
    CHECK(capability_derive(&parent, 0, 0, &child) == 0);
    CHECK(capability_derive(&parent, UINT32_C(0x10), 0, &child) == 0);
    CHECK(capability_derive(NULL, CAP_RIGHT_READ, 0, &child) == 0);
    CHECK(capability_derive(&parent, CAP_RIGHT_READ, 0, NULL) == 0);
}

static void test_capability_table(void) {
    struct capability_table table;
    struct capability_table invalid;
    struct capability root;
    const struct capability *child;
    struct object_id missing;
    uint32_t root_slot;
    uint32_t child_slot;
    uint32_t grant_slot;
    uint32_t grandchild_slot;
    uint32_t found_slot;

    capability_table_init(&table);
    CHECK(capability_table_get(&table, 0) == NULL);
    CHECK(capability_table_is_valid(&table) == 1);

    root = make_capability(CAP_RIGHT_ALL, 7);
    CHECK(capability_table_insert_root(&table, &root, &root_slot) == 1);
    CHECK(root_slot == 0);
    CHECK(table.parents[root_slot] == CAP_SLOT_NONE);
    CHECK(capability_table_find(&table, root.object, &found_slot) == 1);
    CHECK(found_slot == root_slot);

    missing.high = UINT64_C(9);
    missing.low = UINT64_C(9);
    CHECK(capability_table_find(&table, missing, &found_slot) == 0);
    CHECK(capability_table_find(NULL, root.object, &found_slot) == 0);
    CHECK(capability_table_find(&table, root.object, NULL) == 0);

    CHECK(capability_table_derive(
              &table, root_slot, CAP_RIGHT_READ, 99, &child_slot
          ) == 1);
    child = capability_table_get(&table, child_slot);
    CHECK(child != NULL);
    CHECK(child->rights == CAP_RIGHT_READ);
    CHECK(child->badge == 99);
    CHECK(table.parents[child_slot] == root_slot);
    CHECK(capability_table_is_valid(&table) == 1);

    invalid = table;
    invalid.parents[child_slot] = CAP_TABLE_SLOTS;
    CHECK(capability_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.parents[root_slot] = child_slot;
    CHECK(capability_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.slots[child_slot].object.low++;
    CHECK(capability_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.slots[child_slot].rights = CAP_RIGHT_ALL;
    invalid.slots[root_slot].rights = CAP_RIGHT_READ | CAP_RIGHT_GRANT;
    CHECK(capability_table_is_valid(&invalid) == 0);

    CHECK(capability_table_derive(
              &table, child_slot, CAP_RIGHT_READ, 0, NULL
          ) == 0);
    CHECK(capability_table_derive(
              &table, root_slot, UINT32_C(0x10), 0, NULL
          ) == 0);

    CHECK(capability_table_derive(
              &table,
              root_slot,
              CAP_RIGHT_READ | CAP_RIGHT_GRANT,
              10,
              &grant_slot
          ) == 1);
    CHECK(capability_table_derive(
              &table, grant_slot, CAP_RIGHT_READ, 11, &grandchild_slot
          ) == 1);

    CHECK(capability_table_revoke(&table, grant_slot) == 1);
    CHECK(capability_table_get(&table, grant_slot) == NULL);
    CHECK(capability_table_get(&table, grandchild_slot) == NULL);
    CHECK(capability_table_get(&table, root_slot) != NULL);
    CHECK(capability_table_get(&table, child_slot) != NULL);

    CHECK(capability_table_revoke(&table, root_slot) == 1);
    CHECK(capability_table_get(&table, root_slot) == NULL);
    CHECK(capability_table_get(&table, child_slot) == NULL);
    CHECK(capability_table_revoke(&table, root_slot) == 0);
}

int main(void) {
    checks_passed = 0;

    test_validation();
    test_permission_checks();
    test_derivation();
    test_invalid_derivation();
    test_capability_table();

    printf("test_capability: %lu checks passed\n", checks_passed);
    return 0;
}
