#include <stdint.h>
#include <stdio.h>

#include "../../kernel/wasm/component_set.h"
#include "check.h"

unsigned long checks_passed;

static struct object_id make_id(uint64_t number) {
    struct object_id id;

    id.high = UINT64_C(10);
    id.low = number;
    return id;
}

static void test_empty_set(void) {
    struct component_set set;
    struct object_id missing;
    uint32_t index;

    missing = make_id(1);
    component_set_init(&set);

    CHECK(set.count == 0);
    CHECK(component_set_find(&set, missing) == NULL);

    for (index = 0; index < COMPONENT_MAX; index++) {
        CHECK(set.items[index] == NULL);
    }
}

static void test_add_and_find(void) {
    struct component_set set;
    struct component first = {0};
    struct component duplicate = {0};

    component_set_init(&set);

    first.root_id = make_id(1);
    duplicate.root_id = first.root_id;

    CHECK(component_set_add(&set, &first) == 1);
    CHECK(set.count == 1);
    CHECK(component_set_find(&set, first.root_id) == &first);

    CHECK(component_set_add(&set, &first) == 0);
    CHECK(component_set_add(&set, &duplicate) == 0);
    CHECK(set.count == 1);
}

static void test_invalid_add(void) {
    struct component_set set;
    struct component invalid = {0};

    component_set_init(&set);

    CHECK(component_set_add(NULL, &invalid) == 0);
    CHECK(component_set_add(&set, NULL) == 0);
    CHECK(component_set_add(&set, &invalid) == 0);
    CHECK(set.count == 0);
}

static void test_remove(void) {
    struct component_set set;
    struct component first = {0};
    struct component second = {0};
    struct component replacement = {0};
    struct object_id missing;

    component_set_init(&set);

    first.root_id = make_id(1);
    second.root_id = make_id(2);
    replacement.root_id = make_id(3);
    missing = make_id(99);

    CHECK(component_set_add(&set, &first) == 1);
    CHECK(component_set_add(&set, &second) == 1);
    CHECK(set.count == 2);

    CHECK(component_set_remove(&set, first.root_id) == 1);
    CHECK(set.count == 1);
    CHECK(component_set_find(&set, first.root_id) == NULL);
    CHECK(component_set_find(&set, second.root_id) == &second);

    CHECK(component_set_remove(&set, missing) == 0);
    CHECK(set.count == 1);

    CHECK(component_set_add(&set, &replacement) == 1);
    CHECK(set.items[0] == &replacement);
    CHECK(set.count == 2);
}

static void test_full_set(void) {
    struct component_set set;
    struct component components[COMPONENT_MAX + 1] = {0};
    uint32_t index;

    component_set_init(&set);

    for (index = 0; index < COMPONENT_MAX; index++) {
        components[index].root_id = make_id(index + 1);
        CHECK(component_set_add(&set, &components[index]) == 1);
    }

    CHECK(set.count == COMPONENT_MAX);

    components[COMPONENT_MAX].root_id = make_id(COMPONENT_MAX + 1);
    CHECK(component_set_add(&set, &components[COMPONENT_MAX]) == 0);
    CHECK(set.count == COMPONENT_MAX);
}

int main(void) {
    checks_passed = 0;

    test_empty_set();
    test_add_and_find();
    test_invalid_add();
    test_remove();
    test_full_set();

    printf("test_component_set: %lu checks passed\n", checks_passed);

    return 0;
}