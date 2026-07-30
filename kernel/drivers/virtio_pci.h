#ifndef JANI_KERNEL_DRIVERS_VIRTIO_PCI_H
#define JANI_KERNEL_DRIVERS_VIRTIO_PCI_H

#include <stdint.h>

#include "pci.h"

#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_NEEDS_RESET 64u
#define VIRTIO_STATUS_FAILED 128u

#define VIRTIO_FEATURE_VERSION_1 32u

#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2u
#define VIRTIO_PCI_CAP_ISR_CFG 3u
#define VIRTIO_PCI_CAP_DEVICE_CFG 4u

struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
};

_Static_assert(
    sizeof(struct virtio_pci_common_cfg) == 0x38u,
    "virtio_pci_common_cfg must match the virtio 1.1 layout exactly"
);

struct virtio_device {
    struct pci_device pci;
    volatile struct virtio_pci_common_cfg *common;
    volatile uint8_t *notify_base;
    volatile uint8_t *device_cfg;
    volatile uint8_t *isr;
    uint32_t notify_multiplier;
};

int virtio_pci_attach(struct virtio_device *device, uint16_t device_id);
int virtio_pci_reset(struct virtio_device *device);
void virtio_pci_add_status(struct virtio_device *device, uint8_t bits);
uint8_t virtio_pci_status(struct virtio_device *device);
int virtio_pci_negotiate(
    struct virtio_device *device,
    uint64_t wanted,
    uint64_t *accepted_out
);
void virtio_pci_notify(
    struct virtio_device *device,
    uint16_t notify_offset,
    uint16_t queue_index
);

#endif
