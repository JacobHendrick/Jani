#include "object_id.h"

int object_id_is_zero(struct object_id id) {
    return (id.high == 0) && (id.low == 0);
}

int object_id_equal(struct object_id left, struct object_id right) {
    return (left.high == right.high) &&
           (left.low == right.low);
}

int object_id_compare(struct object_id left, struct object_id right) {
    if (left.high < right.high) {
        return -1;
    }

    if (left.high > right.high) {
        return 1;
    }

    if (left.low < right.low) {
        return -1;
    }

    if (left.low > right.low) {
        return 1;
    }

    return 0;
}
