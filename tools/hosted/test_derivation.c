#include <stdint.h>
#include <stdio.h>

#include "../../kernel/cap/derivation.h"
#include "check.h"

unsigned long checks_passed;

static struct capability_ref make_ref_generation(
    uint64_t component,
    uint32_t slot,
    uint32_t generation)
{
    struct object_id id;

    id.high = UINT64_C(9);
    id.low = component;
    return capability_ref_make(id, slot, generation);
}

static struct capability_ref make_ref(uint64_t component, uint32_t slot)
{
    return make_ref_generation(component, slot, 1);
}

static int contains_ref(
    const struct capability_ref *references,
    size_t count,
    struct capability_ref expected)
{
    size_t index;

    for (index = 0; index < count; index++) {
        if (capability_ref_equal(references[index], expected)) {
            return 1;
        }
    }

    return 0;
}

static void test_reference_validation(void)
{
    struct capability_ref reference;

    reference = make_ref(1, 0);
    CHECK(sizeof(reference) == CAPABILITY_REF_SIZE);
    CHECK(sizeof(struct capability_derivation) ==
          CAPABILITY_DERIVATION_SIZE);
    CHECK(capability_ref_is_valid(&reference) == 1);
    CHECK(capability_ref_is_valid(NULL) == 0);
    CHECK(capability_ref_equal(reference, make_ref(1, 0)) == 1);
    CHECK(capability_ref_equal(reference, make_ref(1, 1)) == 0);
    CHECK(capability_ref_equal(reference, make_ref(2, 0)) == 0);
    CHECK(capability_ref_equal(
              reference, make_ref_generation(1, 0, 2)
          ) == 0);

    reference.component_id.high = 0;
    reference.component_id.low = 0;
    CHECK(capability_ref_is_valid(&reference) == 0);

    reference = make_ref(1, CAP_TABLE_SLOTS);
    CHECK(capability_ref_is_valid(&reference) == 0);

    reference = make_ref_generation(1, 0, 0);
    CHECK(capability_ref_is_valid(&reference) == 0);
}

static void test_add_and_parent_lookup(void)
{
    struct capability_derivation_table table;
    struct capability_ref parent;
    struct capability_ref child;
    struct capability_ref found;

    parent = make_ref(1, 0);
    child = make_ref(2, 3);
    capability_derivation_table_init(&table);

    CHECK(table.count == 0);
    CHECK(capability_derivation_table_is_valid(&table) == 1);
    CHECK(capability_derivation_add(&table, parent, child) == 1);
    CHECK(table.count == 1);
    CHECK(capability_derivation_parent(&table, child, &found) == 1);
    CHECK(capability_ref_equal(found, parent) == 1);
    CHECK(capability_derivation_parent(&table, parent, &found) == 0);

    CHECK(capability_derivation_add(&table, parent, child) == 0);
    child = make_ref_generation(2, 3, 2);
    CHECK(capability_derivation_add(&table, parent, child) == 1);
    CHECK(capability_derivation_add(&table, child, child) == 0);
    CHECK(capability_derivation_add(NULL, parent, child) == 0);
    CHECK(capability_derivation_parent(&table, child, NULL) == 0);
    CHECK(table.count == 2);
}

static void test_cycle_rejection(void)
{
    struct capability_derivation_table table;
    struct capability_ref first;
    struct capability_ref second;
    struct capability_ref third;

    first = make_ref(1, 0);
    second = make_ref(2, 0);
    third = make_ref(3, 0);
    capability_derivation_table_init(&table);

    CHECK(capability_derivation_add(&table, first, second) == 1);
    CHECK(capability_derivation_add(&table, second, third) == 1);
    CHECK(capability_derivation_add(&table, third, first) == 0);
    CHECK(table.count == 2);
    CHECK(capability_derivation_table_is_valid(&table) == 1);
}

static void test_validation_rejects_corruption(void)
{
    struct capability_derivation_table table;
    struct capability_derivation_table invalid;
    struct capability_ref first;
    struct capability_ref second;
    struct capability_ref third;

    first = make_ref(1, 0);
    second = make_ref(2, 0);
    third = make_ref(3, 0);
    capability_derivation_table_init(&table);
    CHECK(capability_derivation_add(&table, first, second) == 1);
    CHECK(capability_derivation_add(&table, second, third) == 1);

    invalid = table;
    invalid.count = CAP_DERIVATION_MAX + 1u;
    CHECK(capability_derivation_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.records[2].parent = first;
    CHECK(capability_derivation_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.records[1].child = second;
    CHECK(capability_derivation_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.records[0].parent = third;
    CHECK(capability_derivation_table_is_valid(&invalid) == 0);

    invalid = table;
    invalid.records[0].child.slot = CAP_TABLE_SLOTS;
    CHECK(capability_derivation_table_is_valid(&invalid) == 0);
}

static void test_subtree_removal(void)
{
    struct capability_derivation_table table;
    struct capability_ref removed[CAP_DERIVATION_MAX];
    struct capability_ref root;
    struct capability_ref local_child;
    struct capability_ref remote_child;
    struct capability_ref sibling;
    struct capability_ref grandchild;
    struct capability_ref unrelated_root;
    struct capability_ref unrelated_child;
    size_t removed_count;

    root = make_ref(1, 0);
    local_child = make_ref(1, 1);
    remote_child = make_ref(2, 0);
    sibling = make_ref(3, 0);
    grandchild = make_ref(2, 1);
    unrelated_root = make_ref(4, 0);
    unrelated_child = make_ref(4, 1);

    capability_derivation_table_init(&table);
    CHECK(capability_derivation_add(&table, root, local_child) == 1);
    CHECK(capability_derivation_add(&table, local_child, remote_child) == 1);
    CHECK(capability_derivation_add(&table, root, sibling) == 1);
    CHECK(capability_derivation_add(&table, remote_child, grandchild) == 1);
    CHECK(capability_derivation_add(
              &table, unrelated_root, unrelated_child
          ) == 1);

    removed_count = 99;
    CHECK(capability_derivation_remove_subtree(
              &table, root, removed, 3, &removed_count
          ) == 0);
    CHECK(removed_count == 99);
    CHECK(table.count == 5);

    CHECK(capability_derivation_remove_subtree(
              &table, root, removed, CAP_DERIVATION_MAX, &removed_count
          ) == 1);
    CHECK(removed_count == 4);
    CHECK(contains_ref(removed, removed_count, local_child) == 1);
    CHECK(contains_ref(removed, removed_count, remote_child) == 1);
    CHECK(contains_ref(removed, removed_count, sibling) == 1);
    CHECK(contains_ref(removed, removed_count, grandchild) == 1);
    CHECK(table.count == 1);
    CHECK(capability_derivation_parent(
              &table, unrelated_child, &unrelated_root
          ) == 1);
    CHECK(capability_derivation_table_is_valid(&table) == 1);

    removed_count = 99;
    CHECK(capability_derivation_remove_subtree(
              &table, root, NULL, 0, &removed_count
          ) == 1);
    CHECK(removed_count == 0);
    CHECK(table.count == 1);
}

static void test_out_of_order_subtree(void)
{
    struct capability_derivation_table table;
    struct capability_ref removed[CAP_DERIVATION_MAX];
    struct capability_ref root;
    struct capability_ref child;
    struct capability_ref grandchild;
    struct capability_ref great_grandchild;
    size_t removed_count;

    root = make_ref(1, 0);
    child = make_ref(1, 1);
    grandchild = make_ref(2, 0);
    great_grandchild = make_ref(3, 0);
    capability_derivation_table_init(&table);

    CHECK(capability_derivation_add(
              &table, grandchild, great_grandchild
          ) == 1);
    CHECK(capability_derivation_add(&table, child, grandchild) == 1);
    CHECK(capability_derivation_add(&table, root, child) == 1);
    CHECK(capability_derivation_remove_subtree(
              &table, root, removed, CAP_DERIVATION_MAX, &removed_count
          ) == 1);
    CHECK(removed_count == 3);
    CHECK(table.count == 0);
    CHECK(capability_derivation_table_is_valid(&table) == 1);
}

static void test_full_table(void)
{
    struct capability_derivation_table table;
    struct capability_ref root;
    uint32_t index;

    root = make_ref(1, 0);
    capability_derivation_table_init(&table);

    for (index = 0; index < CAP_DERIVATION_MAX; index++) {
        struct capability_ref child;

        child = make_ref(2u + (index / CAP_TABLE_SLOTS),
                         index % CAP_TABLE_SLOTS);
        CHECK(capability_derivation_add(&table, root, child) == 1);
    }

    CHECK(table.count == CAP_DERIVATION_MAX);
    CHECK(capability_derivation_add(
              &table, root, make_ref(99, 0)
          ) == 0);
    CHECK(capability_derivation_table_is_valid(&table) == 1);
}

int main(void)
{
    checks_passed = 0;

    test_reference_validation();
    test_add_and_parent_lookup();
    test_cycle_rejection();
    test_validation_rejects_corruption();
    test_subtree_removal();
    test_out_of_order_subtree();
    test_full_table();

    printf("test_derivation: %lu checks passed\n", checks_passed);
    return 0;
}
