#ifndef JANI_KERNEL_DRIVERS_VIRTIO_NET_H
#define JANI_KERNEL_DRIVERS_VIRTIO_NET_H

#include <stddef.h>
#include <stdint.h>

#include "../net/frame_decode.h"

#define VIRTIO_NET_RECEIVE_QUEUE 0u
#define VIRTIO_NET_TRANSMIT_QUEUE 1u

#define VIRTIO_NET_HEADER_GSO_NONE 0u
#define VIRTIO_NET_MAX_FRAME_SIZE 1514u
#define VIRTIO_NET_RECEIVE_BUFFER_SIZE \
    (12u + VIRTIO_NET_MAX_FRAME_SIZE)
#define VIRTIO_NET_MIN_FRAME_SIZE 14u
#define VIRTIO_NET_TRANSMIT_BUFFER_SIZE \
    (12u + VIRTIO_NET_MAX_FRAME_SIZE)

struct virtio_net_header {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t header_length;
    uint16_t gso_size;
    uint16_t checksum_start;
    uint16_t checksum_offset;
    uint16_t buffer_count;
};

enum virtio_net_receive_result {
    VIRTIO_NET_RECEIVE_FAILED = -1,
    VIRTIO_NET_RECEIVE_EMPTY = 0,
    VIRTIO_NET_RECEIVE_DROPPED = 1,
    VIRTIO_NET_RECEIVE_PACKET = 2,
    VIRTIO_NET_RECEIVE_BUSY = 3,
};

enum virtio_net_send_result {
    VIRTIO_NET_SEND_INVALID = -2,
    VIRTIO_NET_SEND_FAILED = -1,
    VIRTIO_NET_SEND_BUSY = 0,
    VIRTIO_NET_SEND_SUBMITTED = 1,
};

enum virtio_net_transmit_result {
    VIRTIO_NET_TRANSMIT_FAILED = -1,
    VIRTIO_NET_TRANSMIT_IDLE = 0,
    VIRTIO_NET_TRANSMIT_PENDING = 1,
    VIRTIO_NET_TRANSMIT_COMPLETE = 2,
};

_Static_assert(sizeof(struct virtio_net_header) == 12u,
               "virtio_net_header must match the VirtIO layout");

int virtio_net_init(void);
int virtio_net_is_ready(void);

enum virtio_net_receive_result virtio_net_poll(
    struct jani_udp_datagram *out
);

const uint8_t *virtio_net_received_frame(void);
size_t virtio_net_received_frame_length(void);
int virtio_net_release_receive(void);

enum virtio_net_send_result virtio_net_send(
    const uint8_t *frame,
    size_t frame_length
);

enum virtio_net_transmit_result virtio_net_poll_transmit(void);

#endif
