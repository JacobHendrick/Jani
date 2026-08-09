#include "capability.h"

#include <stddef.h>

int capability_is_valid(const struct capability *capability) {
    if (capability == NULL) {
        return 0;
    }

    if (object_id_is_zero(capability->object)) {
        return 0;
    }

    if (capability->rights == 0) {
        return 0;
    }

    if ((capability->rights & ~CAP_RIGHT_ALL) != 0) {
        return 0;
    }

    return 1;
}

int capability_allows(
    const struct capability *capability,
    uint32_t required_rights
) {
    if (!capability_is_valid(capability)) {
        return 0;
    }

    if ((required_rights == 0) ||
        ((required_rights & ~CAP_RIGHT_ALL) != 0)) {
        return 0;
    }

    return (capability->rights & required_rights) == required_rights;
}

int capability_derive(
    const struct capability *parent,
    uint32_t child_rights,
    uint32_t child_badge,
    struct capability *child_out
) {
    if ((child_out == NULL) || !capability_is_valid(parent)) {
        return 0;
    }

    if (!capability_allows(parent, CAP_RIGHT_GRANT)) {
        return 0;
    }

    if ((child_rights == 0) ||
        ((child_rights & ~CAP_RIGHT_ALL) != 0)) {
        return 0;
    }

    if ((child_rights & parent->rights) != child_rights) {
        return 0;
    }

    child_out->object = parent->object;
    child_out->rights = child_rights;
    child_out->badge = child_badge;

    return 1;
}
