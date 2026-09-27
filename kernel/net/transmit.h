#ifndef JANI_KERNEL_NET_TRANSMIT_H
#define JANI_KERNEL_NET_TRANSMIT_H

#include "frame_encode.h"
#include "../drivers/virtio_net.h"

/* The transport copies the encoded bytes before this call returns. */
enum virtio_net_send_result net_send_udp(
    const struct jani_udp_frame_fields *fields,
    const uint8_t *payload, size_t payload_length);

#endif
