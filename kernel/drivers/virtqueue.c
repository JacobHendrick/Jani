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

    device->common->queue_desc = desc_physical;
    device->common->queue_driver = avail_physical;
    device->common->queue_device = used_physical;
    queue->notify_offset = device->common->queue_notify_off;
    device->common->queue_enable = 1;

    return 1;
}

int virtqueue_submit(
    struct virtqueue *queue,
    const struct virtqueue_buffer *buffers,
    uint16_t count
) {
    uint16_t index;
    uint16_t avail_slot;
    uint32_t spins;

    if ((queue == 0) || (buffers == 0) || (count == 0) ||
        (count > queue->size)) {
        return 0;
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
        queue->desc[index].next = (uint16_t)(index + 1u);
    }

    avail_slot = (uint16_t)(queue->avail->idx % queue->size);
    queue->avail->ring[avail_slot] = 0;

    write_barrier();
    queue->avail->idx = (uint16_t)(queue->avail->idx + 1u);
    write_barrier();

    virtio_pci_notify(queue->device, queue->notify_offset, queue->index);

    for (spins = 0; spins < VIRTQUEUE_POLL_LIMIT; spins++) {
        if (queue->used->idx != queue->last_used) {
            read_barrier();
            queue->last_used = (uint16_t)(queue->last_used + 1u);
            return 1;
        }
        cpu_relax();
    }

    return 0;
}
