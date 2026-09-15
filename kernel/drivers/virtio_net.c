#include <stdint.h>

#include "../lib/printk.h"
#include "pci.h"
#include "virtio_net.h"
#include "virtio_pci.h"
#include "virtqueue.h"

static struct virtio_device net_device;
static struct virtqueue receive_queue;
static struct virtqueue transmit_queue;
static int net_ready;

static int attach_any_virtio_net(void) {
    if (virtio_pci_attach(&net_device, PCI_DEVICE_VIRTIO_NET_MODERN)) {
        return 1;
    }

    return virtio_pci_attach(&net_device,
                             PCI_DEVICE_VIRTIO_NET_TRANSITIONAL);
}

int virtio_net_init(void) {
    uint64_t wanted_features;

    net_ready = 0;

    if (!attach_any_virtio_net()) {
        kputs("ERROR: no virtio-net device found on the PCI bus\n");
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

    virtio_pci_add_status(&net_device, VIRTIO_STATUS_DRIVER_OK);

    if ((virtio_pci_status(&net_device) &
         (VIRTIO_STATUS_NEEDS_RESET | VIRTIO_STATUS_FAILED)) != 0) {
        kputs("ERROR: virtio-net entered a failed state\n");
        return 0;
    }

    net_ready = 1;
    kputs("virtio-net ready: receive and transmit queues configured\n");
    return 1;
}

int virtio_net_is_ready(void) {
    return net_ready;
}
