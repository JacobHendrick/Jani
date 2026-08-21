#ifndef JANI_KERNEL_WASM_COMPONENT_SET_H
#define JANI_KERNEL_WASM_COMPONENT_SET_H

#include "component.h"

struct component_set {
    struct component *items[COMPONENT_MAX];
    uint32_t count;
};

void component_set_init(struct component_set *set);

int component_set_add(
    struct component_set *set,
    struct component *component
);

struct component *component_set_find(
    const struct component_set *set,
    struct object_id root_id
);

int component_set_remove(
    struct component_set *set,
    struct object_id root_id
);

#endif
