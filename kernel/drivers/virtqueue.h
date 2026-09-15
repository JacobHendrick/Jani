#ifndef JANI_KERNEL_DRIVERS_VIRTQUEUE_H
#define JANI_KERNEL_DRIVERS_VIRTQUEUE_H

#include <stdint.h>

#include "virtio_pci.h"

#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u

#define VIRTQUEUE_MAX_SIZE 256u

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

_Static_assert(sizeof(struct virtq_desc) == 16u,
               "virtq_desc must match the virtio 1.1 layout exactly");
_Static_assert(sizeof(struct virtq_used_elem) == 8u,
               "virtq_used_elem must match the virtio 1.1 layout exactly");

struct virtqueue_buffer {
    uint64_t physical_address;
    uint32_t length;
    int device_writable;
};

enum virtqueue_poll_result {
    VIRTQUEUE_POLL_FAILED = -1,
    VIRTQUEUE_POLL_EMPTY = 0,
    VIRTQUEUE_POLL_COMPLETE = 1,
};

struct virtqueue {
    struct virtio_device *device;
    volatile struct virtq_desc *desc;
    volatile struct virtq_avail *avail;
    volatile struct virtq_used *used;
    uint16_t index;
    uint16_t size;
    uint16_t notify_offset;
    uint16_t last_used;
    uint32_t writable_capacity;
    int in_flight;
    int failed;
};

int virtqueue_setup(
    struct virtqueue *queue,
    struct virtio_device *device,
    uint16_t index
);

int virtqueue_submit_async(
    struct virtqueue *queue,
    const struct virtqueue_buffer *buffers,
    uint16_t count
);

enum virtqueue_poll_result virtqueue_poll_used(
    struct virtqueue *queue,
    uint32_t *length_out
);

int virtqueue_submit(
    struct virtqueue *queue,
    const struct virtqueue_buffer *buffers,
    uint16_t count
);

#endif
