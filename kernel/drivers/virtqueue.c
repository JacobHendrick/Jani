#include <stdint.h>

#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "virtqueue.h"

#define VIRTQUEUE_POLL_LIMIT 100000000u

static void write_barrier(void) {
    __asm__ volatile ("sfence" : : : "memory");
}

static void read_barrier(void) {
    __asm__ volatile ("lfence" : : : "memory");
}

static void cpu_relax(void) {
    __asm__ volatile ("pause");
}

static void fail_queue(struct virtqueue *queue) {
    queue->failed = 1;
    queue->in_flight = 0;
    virtio_pci_add_status(queue->device, VIRTIO_STATUS_FAILED);
}

static void *allocate_zeroed_frame(uint64_t *physical_out) {
    uint64_t physical;
    void *virtual_address;

    physical = pmm_alloc_frame();
    if (physical == 0) {
        return 0;
    }

    virtual_address = vmm_physical_to_virtual(physical);
    if (virtual_address == 0) {
        pmm_free_frame(physical);
        return 0;
    }

    memset(virtual_address, 0, PMM_FRAME_SIZE);
    *physical_out = physical;
    return virtual_address;
}

int virtqueue_setup(
    struct virtqueue *queue,
    struct virtio_device *device,
    uint16_t index
) {
    uint64_t desc_physical;
    uint64_t avail_physical;
    uint64_t used_physical;
    void *desc_virtual;
    void *avail_virtual;
    void *used_virtual;
    uint16_t size;

    if ((queue == 0) || (device == 0)) {
        return 0;
    }

    device->common->queue_select = index;

    size = device->common->queue_size;
    if (size == 0) {
        return 0;
    }
    if (size > VIRTQUEUE_MAX_SIZE) {
        size = VIRTQUEUE_MAX_SIZE;
        device->common->queue_size = size;
    }

    desc_virtual = allocate_zeroed_frame(&desc_physical);
    if (desc_virtual == 0) {
        return 0;
    }
    avail_virtual = allocate_zeroed_frame(&avail_physical);
    if (avail_virtual == 0) {
        pmm_free_frame(desc_physical);
        return 0;
    }
    used_virtual = allocate_zeroed_frame(&used_physical);
    if (used_virtual == 0) {
        pmm_free_frame(desc_physical);
        pmm_free_frame(avail_physical);
        return 0;
    }

    queue->device = device;
    queue->desc = desc_virtual;
    queue->avail = avail_virtual;
    queue->used = used_virtual;
    queue->index = index;
    queue->size = size;
    queue->last_used = 0;
    queue->writable_capacity = 0;
    queue->in_flight = 0;
    queue->failed = 0;

    device->common->queue_desc = desc_physical;
    device->common->queue_driver = avail_physical;
    device->common->queue_device = used_physical;
    queue->notify_offset = device->common->queue_notify_off;
    device->common->queue_enable = 1;

    return 1;
}

int virtqueue_submit_async(
    struct virtqueue *queue,
    const struct virtqueue_buffer *buffers,
    uint16_t count
) {
    uint64_t writable_capacity;
    uint16_t index;
    uint16_t avail_slot;

    if ((queue == 0) || (buffers == 0) || (count == 0)) {
        return 0;
    }

    if ((queue->device == 0) || (queue->desc == 0) ||
        (queue->avail == 0) || (queue->used == 0) ||
        (queue->size == 0) || (count > queue->size) ||
        queue->in_flight || queue->failed) {
        return 0;
    }

    writable_capacity = 0;

    for (index = 0; index < count; index++) {
        uint64_t length;

        length = buffers[index].length;

        if ((length != 0) &&
            (buffers[index].physical_address > UINT64_MAX - length)) {
            return 0;
        }

        if (buffers[index].device_writable) {
            writable_capacity += length;

            if (writable_capacity > UINT32_MAX) {
                return 0;
            }
        }
    }

    for (index = 0; index < count; index++) {
        uint16_t flags = 0;

        if (buffers[index].device_writable) {
            flags |= VIRTQ_DESC_F_WRITE;
        }
        if ((uint16_t)(index + 1u) < count) {
            flags |= VIRTQ_DESC_F_NEXT;
        }

        queue->desc[index].addr = buffers[index].physical_address;
        queue->desc[index].len = buffers[index].length;
        queue->desc[index].flags = flags;
        queue->desc[index].next = (uint16_t)(index + 1u) < count
            ? (uint16_t)(index + 1u)
            : 0;
    }

    avail_slot = (uint16_t)(queue->avail->idx % queue->size);
    queue->avail->ring[avail_slot] = 0;

    queue->writable_capacity = (uint32_t)writable_capacity;
    queue->in_flight = 1;

    write_barrier();
    queue->avail->idx = (uint16_t)(queue->avail->idx + 1u);
    write_barrier();

    virtio_pci_notify(queue->device, queue->notify_offset, queue->index);
    return 1;
}

enum virtqueue_poll_result virtqueue_poll_used(
    struct virtqueue *queue,
    uint32_t *length_out
) {
    uint16_t used_index;
    uint16_t used_slot;
    uint32_t used_id;
    uint32_t used_length;

    if ((queue == 0) || (length_out == 0)) {
        return VIRTQUEUE_POLL_FAILED;
    }
    if (queue->failed) {
        return VIRTQUEUE_POLL_FAILED;
    }
    if ((queue->device == 0) || (queue->used == 0) ||
        (queue->size == 0)) {
        return VIRTQUEUE_POLL_FAILED;
    }

    used_index = queue->used->idx;
    if (!queue->in_flight) {
        if (used_index != queue->last_used) {
            fail_queue(queue);
            return VIRTQUEUE_POLL_FAILED;
        }
        return VIRTQUEUE_POLL_EMPTY;
    }
    if (used_index == queue->last_used) {
        return VIRTQUEUE_POLL_EMPTY;
    }

    read_barrier();
    if ((uint16_t)(used_index - queue->last_used) != 1u) {
        fail_queue(queue);
        return VIRTQUEUE_POLL_FAILED;
    }

    used_slot = (uint16_t)(queue->last_used % queue->size);
    used_id = queue->used->ring[used_slot].id;
    used_length = queue->used->ring[used_slot].len;
    if ((used_id != 0) || (used_length > queue->writable_capacity)) {
        fail_queue(queue);
        return VIRTQUEUE_POLL_FAILED;
    }

    queue->last_used = used_index;
    queue->writable_capacity = 0;
    queue->in_flight = 0;
    *length_out = used_length;
    return VIRTQUEUE_POLL_COMPLETE;
}

int virtqueue_submit(
    struct virtqueue *queue,
    const struct virtqueue_buffer *buffers,
    uint16_t count
) {
    uint32_t used_length;
    uint32_t spins;
    enum virtqueue_poll_result result;

    if (!virtqueue_submit_async(queue, buffers, count)) {
        return 0;
    }

    for (spins = 0; spins < VIRTQUEUE_POLL_LIMIT; spins++) {
        result = virtqueue_poll_used(queue, &used_length);
        if (result == VIRTQUEUE_POLL_COMPLETE) {
            return 1;
        }
        if (result == VIRTQUEUE_POLL_FAILED) {
            return 0;
        }
        cpu_relax();
    }

    fail_queue(queue);
    return 0;
}
