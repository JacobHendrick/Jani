#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
static unsigned int allocation_calls;
static unsigned int free_calls;
static unsigned int post_calls;
static int failed_allocation_call;
static uint64_t failed_mapping_physical;
static int post_result;
static enum virtqueue_poll_result poll_result;
static uint32_t completed_length;
static int decode_result;
static unsigned int poll_calls;
static unsigned int decode_calls;
static const uint8_t *decoded_frame;
static size_t decoded_length;
static uint64_t freed_physicals[2];
static struct virtqueue_buffer posted_buffer;
static uint16_t posted_queue_index;
static uint16_t polled_queue_index;
static uint8_t receive_page[4096];
static uint8_t transmit_page[4096];

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
    allocation_calls = 0;
    free_calls = 0;
    post_calls = 0;
    failed_allocation_call = 0;
    failed_mapping_physical = 0;
    post_result = 1;
    poll_result = VIRTQUEUE_POLL_EMPTY;
    completed_length = 0;
    decode_result = 1;
    poll_calls = 0;
    decode_calls = 0;
    decoded_frame = 0;
    decoded_length = 0;
    memset(freed_physicals, 0, sizeof(freed_physicals));
    memset(&posted_buffer, 0, sizeof(posted_buffer));
    posted_queue_index = UINT16_MAX;
    polled_queue_index = UINT16_MAX;
    memset(receive_page, 0xa5, sizeof(receive_page));
    memset(transmit_page, 0xa5, sizeof(transmit_page));
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
    queue->index = index;
    (void)device;
    queue_indices[queue_calls++] = index;
    return index != (uint16_t)failed_queue;
}

int virtqueue_submit_async(struct virtqueue *queue,
                           const struct virtqueue_buffer *buffers,
                           uint16_t count) {
    CHECK(queue != 0);
    CHECK(buffers != 0);
    CHECK(count == 1);
    post_calls++;
    posted_buffer = buffers[0];
    posted_queue_index = queue->index;
    return post_result;
}

enum virtqueue_poll_result virtqueue_poll_used(
    struct virtqueue *queue,
    uint32_t *length_out
) {
    CHECK(queue != 0);
    CHECK(length_out != 0);
    poll_calls++;
    polled_queue_index = queue->index;

    if (poll_result == VIRTQUEUE_POLL_COMPLETE) {
        *length_out = completed_length;
    }

    return poll_result;
}

int jani_udp_frame_decode(
    const uint8_t *frame,
    size_t length,
    struct jani_udp_datagram *out
) {
    CHECK(frame != 0);
    CHECK(out != 0);
    decode_calls++;
    decoded_frame = frame;
    decoded_length = length;

    if (!decode_result) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->source_port = 1234u;
    out->destination_port = 5678u;
    out->payload_offset = 42u;
    out->payload_length = 3u;
    return 1;
}

uint64_t pmm_alloc_frame(void) {
    allocation_calls++;
    if ((int)allocation_calls == failed_allocation_call) {
        return 0;
    }

    CHECK(allocation_calls <= 2);
    return allocation_calls == 1 ? 0x5000u : 0x6000u;
}

void pmm_free_frame(uint64_t physical_address) {
    CHECK(free_calls < 2);
    freed_physicals[free_calls] = physical_address;
    free_calls++;
}

void *vmm_physical_to_virtual(uint64_t physical_address) {
    if (physical_address == failed_mapping_physical) {
        return 0;
    }
    if (physical_address == 0x5000u) {
        return receive_page;
    }

    CHECK(physical_address == 0x6000u);
    return transmit_page;
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
    CHECK(allocation_calls == 2 && post_calls == 1);
    CHECK(posted_buffer.physical_address == 0x5000u);
    CHECK(posted_buffer.length == VIRTIO_NET_RECEIVE_BUFFER_SIZE);
    CHECK(posted_buffer.device_writable == 1);
    CHECK(posted_queue_index == VIRTIO_NET_RECEIVE_QUEUE);
    CHECK(receive_page[0] == 0 && receive_page[sizeof(receive_page) - 1] == 0);
    CHECK(transmit_page[0] == 0 && transmit_page[sizeof(transmit_page) - 1] == 0);
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
    CHECK(free_calls == 2);
    CHECK(freed_physicals[0] == 0x6000u);
    CHECK(freed_physicals[1] == 0x5000u);

    reset_fake_device();
    failed_allocation_call = 1;
    CHECK(virtio_net_init() == 0);
    CHECK(post_calls == 0 && free_calls == 0);

    reset_fake_device();
    failed_mapping_physical = 0x5000u;
    CHECK(virtio_net_init() == 0);
    CHECK(post_calls == 0);
    CHECK(free_calls == 1 && freed_physicals[0] == 0x5000u);

    reset_fake_device();
    failed_allocation_call = 2;
    CHECK(virtio_net_init() == 0);
    CHECK(allocation_calls == 2);
    CHECK(post_calls == 0);
    CHECK(free_calls == 1 && freed_physicals[0] == 0x5000u);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);

    reset_fake_device();
    failed_mapping_physical = 0x6000u;
    CHECK(virtio_net_init() == 0);
    CHECK(allocation_calls == 2);
    CHECK(post_calls == 0);
    CHECK(free_calls == 2);
    CHECK(freed_physicals[0] == 0x6000u);
    CHECK(freed_physicals[1] == 0x5000u);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);

    reset_fake_device();
    post_result = 0;
    CHECK(virtio_net_init() == 0);
    CHECK(post_calls == 1);
    CHECK(free_calls == 2);
    CHECK(freed_physicals[0] == 0x6000u);
    CHECK(freed_physicals[1] == 0x5000u);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
}

static void test_empty_and_invalid_poll(void) {
    struct jani_udp_datagram output;
    struct jani_udp_datagram before;

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    memset(&output, 0x5a, sizeof(output));
    before = output;

    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_EMPTY);
    CHECK(poll_calls == 1);
    CHECK(post_calls == 1);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(virtio_net_received_frame() == 0);
    CHECK(virtio_net_received_frame_length() == 0);

    CHECK(virtio_net_poll(0) == VIRTIO_NET_RECEIVE_FAILED);
    CHECK(poll_calls == 1);
}

static void test_valid_packet_ownership(void) {
    struct jani_udp_datagram output;
    unsigned int polls_before_busy;

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    receive_page[sizeof(struct virtio_net_header)] = 0x45u;
    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = sizeof(struct virtio_net_header) + 45u;

    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_PACKET);
    CHECK(decode_calls == 1);
    CHECK(decoded_frame == receive_page + sizeof(struct virtio_net_header));
    CHECK(decoded_length == 45u);
    CHECK(output.source_port == 1234u);
    CHECK(output.destination_port == 5678u);
    CHECK(virtio_net_received_frame() == decoded_frame);
    CHECK(virtio_net_received_frame_length() == 45u);
    CHECK(post_calls == 1);

    polls_before_busy = poll_calls;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_BUSY);
    CHECK(poll_calls == polls_before_busy);

    CHECK(virtio_net_release_receive() == 1);
    CHECK(post_calls == 2);
    CHECK(receive_page[sizeof(struct virtio_net_header)] == 0);
    CHECK(virtio_net_received_frame() == 0);
    CHECK(virtio_net_received_frame_length() == 0);
    CHECK(virtio_net_release_receive() == 0);
}

static void test_malformed_packets_are_reposted(void) {
    struct jani_udp_datagram output;
    struct jani_udp_datagram before;

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = sizeof(struct virtio_net_header) - 1u;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_DROPPED);
    CHECK(decode_calls == 0);
    CHECK(post_calls == 2);

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    receive_page[1] = 1u;
    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = sizeof(struct virtio_net_header) + 45u;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_DROPPED);
    CHECK(decode_calls == 0);
    CHECK(post_calls == 2);

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    memset(&output, 0x5a, sizeof(output));
    before = output;
    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = sizeof(struct virtio_net_header) + 45u;
    decode_result = 0;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_DROPPED);
    CHECK(decode_calls == 1);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(post_calls == 2);
}

static void test_receive_failures_quarantine_driver(void) {
    struct jani_udp_datagram output;

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    poll_result = VIRTQUEUE_POLL_FAILED;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_FAILED);
    CHECK(virtio_net_is_ready() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
    CHECK(post_calls == 1);

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = 0;
    post_result = 0;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_FAILED);
    CHECK(virtio_net_is_ready() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = sizeof(struct virtio_net_header) + 45u;
    CHECK(virtio_net_poll(&output) == VIRTIO_NET_RECEIVE_PACKET);
    post_result = 0;
    CHECK(virtio_net_release_receive() == 0);
    CHECK(virtio_net_is_ready() == 0);
    CHECK(virtio_net_received_frame() == 0);
}

static void fill_transmit_frame(uint8_t *frame, size_t length) {
    size_t index;

    for (index = 0; index < length; index++) {
        frame[index] = (uint8_t)(index + 1u);
    }
}

static void test_transmit_validation_and_busy_state(void) {
    uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE];
    size_t index;

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));

    CHECK(virtio_net_send(0, VIRTIO_NET_MIN_FRAME_SIZE) ==
          VIRTIO_NET_SEND_INVALID);
    CHECK(virtio_net_send(frame, VIRTIO_NET_MIN_FRAME_SIZE - 1u) ==
          VIRTIO_NET_SEND_INVALID);
    CHECK(virtio_net_send(frame, VIRTIO_NET_MAX_FRAME_SIZE + 1u) ==
          VIRTIO_NET_SEND_INVALID);
    CHECK(post_calls == 1);

    CHECK(virtio_net_send(frame, VIRTIO_NET_MIN_FRAME_SIZE) ==
          VIRTIO_NET_SEND_SUBMITTED);
    CHECK(post_calls == 2);
    CHECK(posted_queue_index == VIRTIO_NET_TRANSMIT_QUEUE);
    CHECK(posted_buffer.physical_address == 0x6000u);
    CHECK(posted_buffer.length == sizeof(struct virtio_net_header) +
                                  VIRTIO_NET_MIN_FRAME_SIZE);
    CHECK(posted_buffer.device_writable == 0);

    for (index = 0; index < sizeof(struct virtio_net_header); index++) {
        CHECK(transmit_page[index] == 0);
    }
    CHECK(memcmp(transmit_page + sizeof(struct virtio_net_header),
                 frame, VIRTIO_NET_MIN_FRAME_SIZE) == 0);
    CHECK(transmit_page[sizeof(struct virtio_net_header) +
                        VIRTIO_NET_MIN_FRAME_SIZE] == 0);

    CHECK(virtio_net_send(frame, VIRTIO_NET_MIN_FRAME_SIZE) ==
          VIRTIO_NET_SEND_BUSY);
    CHECK(post_calls == 2);
}

static void test_maximum_transmit_frame(void) {
    uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE];

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));

    CHECK(virtio_net_send(frame, sizeof(frame)) ==
          VIRTIO_NET_SEND_SUBMITTED);
    CHECK(posted_buffer.length == VIRTIO_NET_TRANSMIT_BUFFER_SIZE);
    CHECK(memcmp(transmit_page + sizeof(struct virtio_net_header),
                 frame, sizeof(frame)) == 0);
}

static void test_transmit_submission_failure(void) {
    uint8_t frame[VIRTIO_NET_MIN_FRAME_SIZE];

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));
    post_result = 0;

    CHECK(virtio_net_send(frame, sizeof(frame)) == VIRTIO_NET_SEND_FAILED);
    CHECK(virtio_net_is_ready() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
    CHECK(post_calls == 2);
    CHECK(transmit_page[sizeof(struct virtio_net_header)] == 0);
    CHECK(virtio_net_send(frame, sizeof(frame)) == VIRTIO_NET_SEND_FAILED);
}

static void test_transmit_completion_and_reuse(void) {
    uint8_t frame[VIRTIO_NET_MIN_FRAME_SIZE];

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));

    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_IDLE);
    CHECK(poll_calls == 0);

    CHECK(virtio_net_send(frame, sizeof(frame)) ==
          VIRTIO_NET_SEND_SUBMITTED);
    poll_result = VIRTQUEUE_POLL_EMPTY;
    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_PENDING);
    CHECK(poll_calls == 1);
    CHECK(polled_queue_index == VIRTIO_NET_TRANSMIT_QUEUE);
    CHECK(transmit_page[sizeof(struct virtio_net_header)] == frame[0]);
    CHECK(virtio_net_send(frame, sizeof(frame)) == VIRTIO_NET_SEND_BUSY);

    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = 0;
    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_COMPLETE);
    CHECK(poll_calls == 2);
    CHECK(transmit_page[sizeof(struct virtio_net_header)] == 0);
    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_IDLE);
    CHECK(poll_calls == 2);

    CHECK(virtio_net_send(frame, sizeof(frame)) ==
          VIRTIO_NET_SEND_SUBMITTED);
    CHECK(post_calls == 3);
}

static void test_transmit_poll_failure(void) {
    uint8_t frame[VIRTIO_NET_MIN_FRAME_SIZE];

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));
    CHECK(virtio_net_send(frame, sizeof(frame)) ==
          VIRTIO_NET_SEND_SUBMITTED);

    poll_result = VIRTQUEUE_POLL_FAILED;
    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_FAILED);
    CHECK(virtio_net_is_ready() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
    CHECK(transmit_page[sizeof(struct virtio_net_header)] == frame[0]);
    CHECK(virtio_net_send(frame, sizeof(frame)) == VIRTIO_NET_SEND_FAILED);
}

static void test_transmit_rejects_malformed_completion(void) {
    uint8_t frame[VIRTIO_NET_MIN_FRAME_SIZE];

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));
    CHECK(virtio_net_send(frame, sizeof(frame)) ==
          VIRTIO_NET_SEND_SUBMITTED);

    poll_result = VIRTQUEUE_POLL_COMPLETE;
    completed_length = 1;
    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_FAILED);
    CHECK(virtio_net_is_ready() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
    CHECK(transmit_page[sizeof(struct virtio_net_header)] == frame[0]);

    reset_fake_device();
    CHECK(virtio_net_init() == 1);
    fill_transmit_frame(frame, sizeof(frame));
    CHECK(virtio_net_send(frame, sizeof(frame)) ==
          VIRTIO_NET_SEND_SUBMITTED);

    poll_result = (enum virtqueue_poll_result)99;
    CHECK(virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_FAILED);
    CHECK(virtio_net_is_ready() == 0);
    CHECK((status_bits & VIRTIO_STATUS_FAILED) != 0);
}

int main(void) {
    test_modern_device();
    test_transitional_device();
    test_failures();
    test_empty_and_invalid_poll();
    test_valid_packet_ownership();
    test_malformed_packets_are_reposted();
    test_receive_failures_quarantine_driver();
    test_transmit_validation_and_busy_state();
    test_maximum_transmit_frame();
    test_transmit_submission_failure();
    test_transmit_completion_and_reuse();
    test_transmit_poll_failure();
    test_transmit_rejects_malformed_completion();
    printf("test_virtio_net: %lu checks passed\n", checks_passed);
    return 0;
}
