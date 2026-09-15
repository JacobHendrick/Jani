#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/drivers/virtqueue.h"
#include "check.h"

unsigned long checks_passed;

struct test_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[4];
};

struct test_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[4];
};

static struct virtq_desc descriptors[4];
static struct test_avail available;
static struct test_used used;
static struct virtio_device device;
static struct virtqueue queue;
static unsigned int notify_count;
static uint8_t status_bits;
static int complete_on_notify;

static void reset_queue(void) {
    memset(descriptors, 0, sizeof(descriptors));
    memset(&available, 0, sizeof(available));
    memset(&used, 0, sizeof(used));
    memset(&device, 0, sizeof(device));
    memset(&queue, 0, sizeof(queue));
    queue.device = &device;
    queue.desc = descriptors;
    queue.avail = (struct virtq_avail *)&available;
    queue.used = (struct virtq_used *)&used;
    queue.index = 7;
    queue.size = 4;
    queue.notify_offset = 9;
    notify_count = 0;
    status_bits = 0;
    complete_on_notify = 0;
}

void virtio_pci_notify(struct virtio_device *notified_device,
                       uint16_t notify_offset, uint16_t queue_index) {
    uint16_t slot;

    CHECK(notified_device == &device);
    CHECK(notify_offset == 9);
    CHECK(queue_index == 7);
    notify_count++;
    if (complete_on_notify) {
        slot = (uint16_t)(queue.last_used % queue.size);
        queue.used->ring[slot].id = 0;
        queue.used->ring[slot].len = queue.writable_capacity;
        queue.used->idx = (uint16_t)(queue.last_used + 1u);
    }
}

void virtio_pci_add_status(struct virtio_device *failed_device, uint8_t bits) {
    CHECK(failed_device == &device);
    status_bits = (uint8_t)(status_bits | bits);
}

uint64_t pmm_alloc_frame(void) {
    return 0;
}

void pmm_free_frame(uint64_t physical_address) {
    (void)physical_address;
}

void *vmm_physical_to_virtual(uint64_t physical_address) {
    (void)physical_address;
    return 0;
}

static void test_async_completion(void) {
    const struct virtqueue_buffer buffers[] = {
        {0x1000, 16, 0},
        {0x2000, 64, 1},
    };
    uint32_t length = UINT32_MAX;

    reset_queue();
    CHECK(virtqueue_submit_async(&queue, buffers, 2) == 1);
    CHECK(queue.in_flight == 1 && queue.writable_capacity == 64);
    CHECK(descriptors[0].addr == 0x1000 && descriptors[0].len == 16);
    CHECK(descriptors[0].flags == VIRTQ_DESC_F_NEXT);
    CHECK(descriptors[0].next == 1);
    CHECK(descriptors[1].flags == VIRTQ_DESC_F_WRITE);
    CHECK(descriptors[1].next == 0);
    CHECK(available.ring[0] == 0 && available.idx == 1);
    CHECK(notify_count == 1);
    CHECK(virtqueue_submit_async(&queue, buffers, 2) == 0);
    CHECK(available.idx == 1 && notify_count == 1);
    CHECK(virtqueue_poll_used(&queue, &length) == VIRTQUEUE_POLL_EMPTY);
    CHECK(length == UINT32_MAX);

    used.ring[0].id = 0;
    used.ring[0].len = 64;
    used.idx = 1;
    CHECK(virtqueue_poll_used(&queue, &length) == VIRTQUEUE_POLL_COMPLETE);
    CHECK(length == 64);
    CHECK(queue.last_used == 1 && queue.in_flight == 0);
    CHECK(virtqueue_poll_used(&queue, &length) == VIRTQUEUE_POLL_EMPTY);
}

static void test_validation_before_publish(void) {
    const struct virtqueue_buffer overflow = {UINT64_MAX, 2, 1};
    const struct virtqueue_buffer too_much[] = {
        {0x1000, UINT32_MAX, 1},
        {0x2000, 1, 1},
    };

    reset_queue();
    CHECK(virtqueue_submit_async(0, &overflow, 1) == 0);
    CHECK(virtqueue_submit_async(&queue, 0, 1) == 0);
    CHECK(virtqueue_submit_async(&queue, &overflow, 0) == 0);
    CHECK(virtqueue_submit_async(&queue, &overflow, 5) == 0);
    CHECK(virtqueue_submit_async(&queue, &overflow, 1) == 0);
    CHECK(virtqueue_submit_async(&queue, too_much, 2) == 0);
    CHECK(descriptors[0].addr == 0);
    CHECK(available.idx == 0 && notify_count == 0);
}

static void expect_bad_completion(uint32_t id, uint32_t length,
                                  uint16_t used_index) {
    const struct virtqueue_buffer buffer = {0x3000, 64, 1};
    uint32_t output = 0xa5a5a5a5u;

    reset_queue();
    CHECK(virtqueue_submit_async(&queue, &buffer, 1) == 1);
    used.ring[0].id = id;
    used.ring[0].len = length;
    used.idx = used_index;
    CHECK(virtqueue_poll_used(&queue, &output) == VIRTQUEUE_POLL_FAILED);
    CHECK(output == 0xa5a5a5a5u);
    CHECK(queue.failed == 1 && queue.in_flight == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
    CHECK(virtqueue_submit_async(&queue, &buffer, 1) == 0);
}

static void test_bad_completions(void) {
    uint32_t output = 7;

    expect_bad_completion(1, 64, 1);
    expect_bad_completion(0, 65, 1);
    expect_bad_completion(0, 64, 2);

    reset_queue();
    used.idx = 1;
    CHECK(virtqueue_poll_used(&queue, &output) == VIRTQUEUE_POLL_FAILED);
    CHECK(queue.failed == 1);
}

static void test_blocking_wrapper(void) {
    const struct virtqueue_buffer buffer = {0x4000, 32, 1};

    reset_queue();
    complete_on_notify = 1;
    CHECK(virtqueue_submit(&queue, &buffer, 1) == 1);
    CHECK(queue.in_flight == 0 && queue.last_used == 1);
}

int main(void) {
    test_async_completion();
    test_validation_before_publish();
    test_bad_completions();
    test_blocking_wrapper();
    printf("test_virtqueue: %lu checks passed\n", checks_passed);
    return 0;
}
