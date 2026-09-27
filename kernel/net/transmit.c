#include "transmit.h"

enum virtio_net_send_result net_send_udp(
    const struct jani_udp_frame_fields *fields,
    const uint8_t *payload, size_t payload_length) {
    uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE];
    size_t length = jani_udp_frame_encode(frame, sizeof(frame), fields,
                                          payload, payload_length);
    if (length == 0) return VIRTIO_NET_SEND_INVALID;
    return virtio_net_send(frame, length);
}
