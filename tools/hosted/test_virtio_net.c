#include <stdint.h>
#include <stdio.h>

#include "../../kernel/drivers/pci.h"
#include "../../kernel/drivers/virtio_net.h"
#include "../../kernel/drivers/virtio_pci.h"
#include "../../kernel/drivers/virtqueue.h"
#include "check.h"

unsigned long checks_passed;

static int modern_present;
static int transitional_present;
static int reset_result;
static int negotiate_result;
static int failed_queue;
static uint8_t forced_status;
static uint8_t status_bits;
static uint16_t attached_ids[2];
static uint16_t queue_indices[2];
static uint64_t negotiated_features;
static unsigned int attach_calls;
static unsigned int reset_calls;
static unsigned int negotiate_calls;
static unsigned int queue_calls;

static void reset_fake_device(void) {
    modern_present = 1;
    transitional_present = 1;
    reset_result = 1;
    negotiate_result = 1;
    failed_queue = -1;
    forced_status = 0;
    status_bits = 0;
    negotiated_features = 0;
    attach_calls = 0;
    reset_calls = 0;
    negotiate_calls = 0;
    queue_calls = 0;
}

int virtio_pci_attach(struct virtio_device *device, uint16_t device_id) {
    (void)device;
    attached_ids[attach_calls++] = device_id;
    if (device_id == PCI_DEVICE_VIRTIO_NET_MODERN) {
        return modern_present;
    }
    return transitional_present;
}

int virtio_pci_reset(struct virtio_device *device) {
    (void)device;
    reset_calls++;
    return reset_result;
}

void virtio_pci_add_status(struct virtio_device *device, uint8_t bits) {
    (void)device;
    status_bits = (uint8_t)(status_bits | bits);
}

uint8_t virtio_pci_status(struct virtio_device *device) {
    (void)device;
    return (uint8_t)(status_bits | forced_status);
}

int virtio_pci_negotiate(struct virtio_device *device, uint64_t wanted,
                         uint64_t *accepted_out) {
    (void)device;
    negotiate_calls++;
    negotiated_features = wanted;
    if (accepted_out != 0) {
        *accepted_out = wanted;
    }
    return negotiate_result;
}

int virtqueue_setup(struct virtqueue *queue, struct virtio_device *device,
                    uint16_t index) {
    (void)queue;
    (void)device;
    queue_indices[queue_calls++] = index;
    return index != (uint16_t)failed_queue;
}

void kputs(const char *message) {
    (void)message;
}

static void test_modern_device(void) {
    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    CHECK(virtio_net_is_ready() == 1);
    CHECK(attach_calls == 1);
    CHECK(attached_ids[0] == PCI_DEVICE_VIRTIO_NET_MODERN);
    CHECK(reset_calls == 1);
    CHECK(negotiate_calls == 1);
    CHECK(negotiated_features == (1ULL << VIRTIO_FEATURE_VERSION_1));
    CHECK(queue_calls == 2);
    CHECK(queue_indices[0] == VIRTIO_NET_RECEIVE_QUEUE);
    CHECK(queue_indices[1] == VIRTIO_NET_TRANSMIT_QUEUE);
    CHECK((status_bits & VIRTIO_STATUS_DRIVER_OK) != 0);
}

static void test_transitional_device(void) {
    reset_fake_device();
    modern_present = 0;
    CHECK(virtio_net_init() == 1);
    CHECK(attach_calls == 2);
    CHECK(attached_ids[1] == PCI_DEVICE_VIRTIO_NET_TRANSITIONAL);
}

static void test_failures(void) {
    reset_fake_device();
    modern_present = 0;
    transitional_present = 0;
    CHECK(virtio_net_init() == 0);
    CHECK(virtio_net_is_ready() == 0);
    CHECK(reset_calls == 0);

    reset_fake_device();
    reset_result = 0;
    CHECK(virtio_net_init() == 0);
    CHECK(negotiate_calls == 0);

    reset_fake_device();
    negotiate_result = 0;
    CHECK(virtio_net_init() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);

    reset_fake_device();
    failed_queue = VIRTIO_NET_RECEIVE_QUEUE;
    CHECK(virtio_net_init() == 0);
    CHECK(queue_calls == 1);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);

    reset_fake_device();
    failed_queue = VIRTIO_NET_TRANSMIT_QUEUE;
    CHECK(virtio_net_init() == 0);
    CHECK(queue_calls == 2);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);

    reset_fake_device();
    forced_status = VIRTIO_STATUS_NEEDS_RESET;
    CHECK(virtio_net_init() == 0);
    CHECK(virtio_net_is_ready() == 0);
}

int main(void) {
    test_modern_device();
    test_transitional_device();
    test_failures();
    printf("test_virtio_net: %lu checks passed\n", checks_passed);
    return 0;
}
