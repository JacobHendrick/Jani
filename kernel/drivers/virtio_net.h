#ifndef JANI_KERNEL_DRIVERS_VIRTIO_NET_H
#define JANI_KERNEL_DRIVERS_VIRTIO_NET_H

#include <stdint.h>

#define VIRTIO_NET_RECEIVE_QUEUE 0u
#define VIRTIO_NET_TRANSMIT_QUEUE 1u

#define VIRTIO_NET_HEADER_GSO_NONE 0u

struct virtio_net_header {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t header_length;
    uint16_t gso_size;
    uint16_t checksum_start;
    uint16_t checksum_offset;
    uint16_t buffer_count;
};

_Static_assert(sizeof(struct virtio_net_header) == 12u,
               "virtio_net_header must match the VirtIO layout");

int virtio_net_init(void);
int virtio_net_is_ready(void);

#endif
