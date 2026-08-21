#include <stddef.h>

#include "component_set.h"

void component_set_init(struct component_set *set) {
    uint32_t index;

    if (set == NULL) {
        return;
    }

    for (index = 0; index < COMPONENT_MAX; index++) {
        set->items[index] = NULL;
    }

    set->count = 0;
}

struct component *component_set_find(
    const struct component_set *set,
    struct object_id root_id
) {
    struct component *component;
    uint32_t index;

    if ((set == NULL) || object_id_is_zero(root_id)) {
        return NULL;
    }

    for (index = 0; index < COMPONENT_MAX; index++) {
        component = set->items[index];

        if ((component != NULL) &&
            object_id_equal(component->root_id, root_id)) {
            return component;
        }
    }

    return NULL;
}

int component_set_add(
    struct component_set *set,
    struct component *component
) {
    uint32_t index;

    if ((set == NULL) || (component == NULL) ||
        object_id_is_zero(component->root_id)) {
        return 0;
    }

    if (component_set_find(set, component->root_id) != NULL) {
        return 0;
    }

    for (index = 0; index < COMPONENT_MAX; index++) {
        if (set->items[index] == NULL) {
            set->items[index] = component;
            set->count++;
            return 1;
        }
    }

    return 0;
}

int component_set_remove(
    struct component_set *set,
    struct object_id root_id
) {
    struct component *component;
    uint32_t index;

    if ((set == NULL) || object_id_is_zero(root_id)) {
        return 0;
    }

    for (index = 0; index < COMPONENT_MAX; index++) {
        component = set->items[index];

        if ((component != NULL) &&
            object_id_equal(component->root_id, root_id)) {
            set->items[index] = NULL;
            set->count--;
            return 1;
        }
    }

    return 0;
}
