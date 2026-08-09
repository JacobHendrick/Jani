#ifndef JANI_KERNEL_CAP_CAPABILITY_H
#define JANI_KERNEL_CAP_CAPABILITY_H

#include <stdint.h>

#include "../obj/object_id.h"

#define CAP_RIGHT_READ  UINT32_C(0x01)
#define CAP_RIGHT_WRITE UINT32_C(0x02)
#define CAP_RIGHT_SEND  UINT32_C(0x04)
#define CAP_RIGHT_GRANT UINT32_C(0x08)

#define CAP_RIGHT_ALL \
    (CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_SEND | CAP_RIGHT_GRANT)

#define CAPABILITY_SIZE 24u

struct capability {
    struct object_id object;
    uint32_t rights;
    uint32_t badge;
};

_Static_assert(
    sizeof(struct capability) == CAPABILITY_SIZE,
    "capability must be exactly 24 bytes"
);

int capability_is_valid(const struct capability *capability);

int capability_allows(
    const struct capability *capability,
    uint32_t required_rights
);

int capability_derive(
    const struct capability *parent,
    uint32_t child_rights,
    uint32_t child_badge,
    struct capability *child_out
);

#endif
