#include <stddef.h>
#include <stdint.h>

#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "pci.h"
#include "virtio_net.h"
#include "virtio_pci.h"
#include "virtqueue.h"

_Static_assert(VIRTIO_NET_RECEIVE_BUFFER_SIZE <= PMM_FRAME_SIZE,
               "virtio-net receive buffer must fit in one physical frame");
_Static_assert(VIRTIO_NET_TRANSMIT_BUFFER_SIZE <= PMM_FRAME_SIZE,
               "virtio-net transmit buffer must fit in one physical frame");

static struct virtio_device net_device;
static struct virtqueue receive_queue;
static struct virtqueue transmit_queue;
static int net_ready;
static uint64_t receive_physical;
static uint8_t *receive_virtual;
static size_t receive_frame_length;
static int receive_held;
static uint64_t transmit_physical;
static uint8_t *transmit_virtual;
static int transmit_in_flight;

static int attach_any_virtio_net(void) {
    if (virtio_pci_attach(&net_device, PCI_DEVICE_VIRTIO_NET_MODERN)) {
        return 1;
    }

    return virtio_pci_attach(&net_device,
                             PCI_DEVICE_VIRTIO_NET_TRANSITIONAL);
}

static int allocate_receive_buffer(void) {
    void *virtual_address;

    receive_physical = pmm_alloc_frame();
    if (receive_physical == 0) {
        return 0;
    }

    virtual_address = vmm_physical_to_virtual(receive_physical);
    if (virtual_address == 0) {
        pmm_free_frame(receive_physical);
        receive_physical = 0;
        return 0;
    }

    receive_virtual = virtual_address;
    memset(receive_virtual, 0, PMM_FRAME_SIZE);
    return 1;
}

static void release_receive_buffer(void) {
    if (receive_physical != 0) {
        pmm_free_frame(receive_physical);
    }
    receive_physical = 0;
    receive_virtual = 0;
}

static int allocate_transmit_buffer(void) {
    void *virtual_address;

    transmit_physical = pmm_alloc_frame();
    if (transmit_physical == 0) {
        return 0;
    }

    virtual_address = vmm_physical_to_virtual(transmit_physical);
    if (virtual_address == 0) {
        pmm_free_frame(transmit_physical);
        transmit_physical = 0;
        return 0;
    }

    transmit_virtual = virtual_address;
    memset(transmit_virtual, 0, PMM_FRAME_SIZE);
    return 1;
}

static void release_transmit_buffer(void) {
    if (transmit_physical != 0) {
        pmm_free_frame(transmit_physical);
    }

    transmit_physical = 0;
    transmit_virtual = 0;
    transmit_in_flight = 0;
}

static int post_receive_buffer(void) {
    struct virtqueue_buffer buffer;

    buffer.physical_address = receive_physical;
    buffer.length = VIRTIO_NET_RECEIVE_BUFFER_SIZE;
    buffer.device_writable = 1;

    return virtqueue_submit_async(&receive_queue, &buffer, 1);
}

static int repost_receive_buffer(void) {
    memset(receive_virtual, 0, PMM_FRAME_SIZE);

    if (!post_receive_buffer()) {
        net_ready = 0;
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        return 0;
    }

    return 1;
}

int virtio_net_init(void) {
    uint64_t wanted_features;

    net_ready = 0;

    receive_frame_length = 0;
    receive_held = 0;
    transmit_in_flight = 0;

    if (!attach_any_virtio_net()) {
        kputs("virtio-net: no device found\n");
        return 0;
    }

    if (!virtio_pci_reset(&net_device)) {
        kputs("ERROR: virtio-net did not acknowledge reset\n");
        return 0;
    }

    virtio_pci_add_status(&net_device, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_pci_add_status(&net_device, VIRTIO_STATUS_DRIVER);

    wanted_features = 1ULL << VIRTIO_FEATURE_VERSION_1;

    if (!virtio_pci_negotiate(
            &net_device,
            wanted_features,
            0
        )) {
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-net feature negotiation failed\n");
        return 0;
    }

    if (!virtqueue_setup(
            &receive_queue,
            &net_device,
            VIRTIO_NET_RECEIVE_QUEUE
        )) {
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-net receive queue setup failed\n");
        return 0;
    }

    if (!virtqueue_setup(
            &transmit_queue,
            &net_device,
            VIRTIO_NET_TRANSMIT_QUEUE
        )) {
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-net transmit queue setup failed\n");
        return 0;
    }

    if (!allocate_receive_buffer()) {
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-net could not allocate receive buffer\n");
        return 0;
    }

    if (!allocate_transmit_buffer()) {
        release_receive_buffer();
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-net could not allocate transmit buffer\n");
        return 0;
    }

    virtio_pci_add_status(&net_device, VIRTIO_STATUS_DRIVER_OK);

    if ((virtio_pci_status(&net_device) &
         (VIRTIO_STATUS_NEEDS_RESET | VIRTIO_STATUS_FAILED)) != 0) {
        release_transmit_buffer();
        release_receive_buffer();
        kputs("ERROR: virtio-net entered a failed state\n");
        return 0;
    }

    if (!post_receive_buffer()) {
        release_transmit_buffer();
        release_receive_buffer();
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-net could not post receive buffer\n");
        return 0;
    }

    net_ready = 1;
    kputs("virtio-net ready: receive and transmit queues configured\n");
    return 1;
}

int virtio_net_is_ready(void) {
    return net_ready;
}

enum virtio_net_receive_result virtio_net_poll(
    struct jani_udp_datagram *out
) {
    enum virtqueue_poll_result result;
    uint32_t used_length;
    const uint8_t *frame;
    size_t frame_length;

    if (!net_ready || out == 0) {
        return VIRTIO_NET_RECEIVE_FAILED;
    }

    if (receive_held) {
        return VIRTIO_NET_RECEIVE_BUSY;
    }

    result = virtqueue_poll_used(&receive_queue, &used_length);

    if (result == VIRTQUEUE_POLL_EMPTY) {
        return VIRTIO_NET_RECEIVE_EMPTY;
    }

    if (result == VIRTQUEUE_POLL_FAILED) {
        net_ready = 0;
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        return VIRTIO_NET_RECEIVE_FAILED;
    }

    if (used_length < sizeof(struct virtio_net_header)) {
        if (!repost_receive_buffer()) {
            return VIRTIO_NET_RECEIVE_FAILED;
        }
        return VIRTIO_NET_RECEIVE_DROPPED;
    }

    if (receive_virtual[1] != VIRTIO_NET_HEADER_GSO_NONE) {
        if (!repost_receive_buffer()) {
            return VIRTIO_NET_RECEIVE_FAILED;
        }
        return VIRTIO_NET_RECEIVE_DROPPED;
    }

    frame = receive_virtual + sizeof(struct virtio_net_header);
    frame_length = used_length - sizeof(struct virtio_net_header);

    if (!jani_udp_frame_decode(frame, frame_length, out)) {
        if (!repost_receive_buffer()) {
            return VIRTIO_NET_RECEIVE_FAILED;
        }
        return VIRTIO_NET_RECEIVE_DROPPED;
    }

    receive_frame_length = frame_length;
    receive_held = 1;
    return VIRTIO_NET_RECEIVE_PACKET;
}

const uint8_t *virtio_net_received_frame(void) {
    if (!receive_held) {
        return 0;
    }

    return receive_virtual + sizeof(struct virtio_net_header);
}

size_t virtio_net_received_frame_length(void) {
    return receive_held ? receive_frame_length : 0;
}

int virtio_net_release_receive(void) {
    if (!net_ready || !receive_held) {
        return 0;
    }

    receive_held = 0;
    receive_frame_length = 0;
    return repost_receive_buffer();
}

enum virtio_net_send_result virtio_net_send(
    const uint8_t *frame,
    size_t frame_length
) {
    struct virtqueue_buffer buffer;
    size_t packet_length;

    if ((frame == 0) ||
        (frame_length < VIRTIO_NET_MIN_FRAME_SIZE) ||
        (frame_length > VIRTIO_NET_MAX_FRAME_SIZE)) {
        return VIRTIO_NET_SEND_INVALID;
    }

    if (!net_ready || (transmit_virtual == 0)) {
        return VIRTIO_NET_SEND_FAILED;
    }

    if (transmit_in_flight) {
        return VIRTIO_NET_SEND_BUSY;
    }

    memset(transmit_virtual, 0, PMM_FRAME_SIZE);

    memcpy(
        transmit_virtual + sizeof(struct virtio_net_header),
        frame,
        frame_length
    );

    packet_length = sizeof(struct virtio_net_header) + frame_length;

    buffer.physical_address = transmit_physical;
    buffer.length = (uint32_t)packet_length;
    buffer.device_writable = 0;

    if (!virtqueue_submit_async(&transmit_queue, &buffer, 1)) {
        memset(transmit_virtual, 0, PMM_FRAME_SIZE);
        net_ready = 0;
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        return VIRTIO_NET_SEND_FAILED;
    }

    transmit_in_flight = 1;
    return VIRTIO_NET_SEND_SUBMITTED;
}

enum virtio_net_transmit_result virtio_net_poll_transmit(void) {
    enum virtqueue_poll_result result;
    uint32_t used_length;

    if (!net_ready) {
        return VIRTIO_NET_TRANSMIT_FAILED;
    }

    if (!transmit_in_flight) {
        return VIRTIO_NET_TRANSMIT_IDLE;
    }

    result = virtqueue_poll_used(&transmit_queue, &used_length);

    if (result == VIRTQUEUE_POLL_EMPTY) {
        return VIRTIO_NET_TRANSMIT_PENDING;
    }

    if (result == VIRTQUEUE_POLL_FAILED) {
        net_ready = 0;
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        return VIRTIO_NET_TRANSMIT_FAILED;
    }

    if ((result != VIRTQUEUE_POLL_COMPLETE) || (used_length != 0)) {
        net_ready = 0;
        virtio_pci_add_status(&net_device, VIRTIO_STATUS_FAILED);
        return VIRTIO_NET_TRANSMIT_FAILED;
    }

    memset(transmit_virtual, 0, PMM_FRAME_SIZE);
    transmit_in_flight = 0;

    return VIRTIO_NET_TRANSMIT_COMPLETE;
}
