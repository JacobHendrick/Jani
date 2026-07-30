#ifndef JANI_KERNEL_OBJ_OBJECT_ID_H
#define JANI_KERNEL_OBJ_OBJECT_ID_H


#include <stdint.h>

struct object_id {
    uint64_t high;
    uint64_t low;
};


int object_id_is_zero(struct object_id id);
int object_id_equal(struct object_id left, struct object_id right);
int object_id_compare(struct object_id left, struct object_id right);

#endif