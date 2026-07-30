#include <stdint.h>

#include "../mm/mmio.h"
#include "pci.h"
#include "virtio_pci.h"

#define VIRTIO_CAP_OFFSET_CFG_TYPE 3u
#define VIRTIO_CAP_OFFSET_BAR 4u
#define VIRTIO_CAP_OFFSET_REGION_OFFSET 8u
#define VIRTIO_CAP_OFFSET_REGION_LENGTH 12u
#define VIRTIO_CAP_OFFSET_NOTIFY_MULTIPLIER 16u

#define VIRTIO_RESET_SPIN_LIMIT 1000000u

static void *map_capability_region(
    const struct pci_device *pci,
    uint8_t capability
) {
    uint8_t bar_index;
    uint32_t region_offset;
    uint32_t region_length;
    uint64_t bar_base;

    bar_index = pci_config_read8(pci,
                                 (uint8_t)(capability + VIRTIO_CAP_OFFSET_BAR));
    region_offset = pci_config_read32(
        pci, (uint8_t)(capability + VIRTIO_CAP_OFFSET_REGION_OFFSET)
    );
    region_length = pci_config_read32(
        pci, (uint8_t)(capability + VIRTIO_CAP_OFFSET_REGION_LENGTH)
    );

    if (region_length == 0) {
        return 0;
    }

    bar_base = pci_bar_address(pci, bar_index);
    if (bar_base == 0) {
        return 0;
    }

    return mmio_map(bar_base + (uint64_t)region_offset,
                    (uint64_t)region_length);
}

int virtio_pci_attach(struct virtio_device *device, uint16_t device_id) {
    uint8_t capability;
    uint32_t steps;

    if (device == 0) {
        return 0;
    }

    device->common = 0;
    device->notify_base = 0;
    device->device_cfg = 0;
    device->isr = 0;
    device->notify_multiplier = 0;

    if (!pci_find_device(PCI_VENDOR_VIRTIO, device_id, &device->pci)) {
        return 0;
    }

    pci_enable_bus_master(&device->pci);

    capability = 0;
    steps = 0;
    for (;;) {
        uint8_t cfg_type;
        void *region;

        capability = pci_capability_find(&device->pci,
                                         PCI_CAPABILITY_ID_VENDOR, capability);
        if (capability == 0) {
            break;
        }
        if (++steps > 48u) {
            break;
        }

        cfg_type = pci_config_read8(
            &device->pci, (uint8_t)(capability + VIRTIO_CAP_OFFSET_CFG_TYPE)
        );

        if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
            region = map_capability_region(&device->pci, capability);
            if (region == 0) {
                return 0;
            }
            device->common = region;
        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
            region = map_capability_region(&device->pci, capability);
            if (region == 0) {
                return 0;
            }
            device->notify_base = region;
            device->notify_multiplier = pci_config_read32(
                &device->pci,
                (uint8_t)(capability + VIRTIO_CAP_OFFSET_NOTIFY_MULTIPLIER)
            );
        } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
            region = map_capability_region(&device->pci, capability);
            if (region == 0) {
                return 0;
            }
            device->device_cfg = region;
        } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
            region = map_capability_region(&device->pci, capability);
            if (region == 0) {
                return 0;
            }
            device->isr = region;
        }
    }

    return (device->common != 0) && (device->notify_base != 0) &&
           (device->device_cfg != 0);
}

int virtio_pci_reset(struct virtio_device *device) {
    uint32_t spins;

    device->common->device_status = 0;

    for (spins = 0; spins < VIRTIO_RESET_SPIN_LIMIT; spins++) {
        if (device->common->device_status == 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }

    return 0;
}

void virtio_pci_add_status(struct virtio_device *device, uint8_t bits) {
    device->common->device_status =
        (uint8_t)(device->common->device_status | bits);
}

uint8_t virtio_pci_status(struct virtio_device *device) {
    return device->common->device_status;
}

int virtio_pci_negotiate(
    struct virtio_device *device,
    uint64_t wanted,
    uint64_t *accepted_out
) {
    uint64_t offered;
    uint64_t accepted;

    device->common->device_feature_select = 0;
    offered = (uint64_t)device->common->device_feature;
    device->common->device_feature_select = 1;
    offered |= (uint64_t)device->common->device_feature << 32;

    accepted = offered & wanted;
    if ((accepted & (1ULL << VIRTIO_FEATURE_VERSION_1)) == 0) {
        return 0;
    }

    device->common->driver_feature_select = 0;
    device->common->driver_feature = (uint32_t)(accepted & 0xFFFFFFFFu);
    device->common->driver_feature_select = 1;
    device->common->driver_feature = (uint32_t)(accepted >> 32);

    virtio_pci_add_status(device, VIRTIO_STATUS_FEATURES_OK);
    if ((virtio_pci_status(device) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        return 0;
    }

    if (accepted_out != 0) {
        *accepted_out = accepted;
    }
    return 1;
}

void virtio_pci_notify(
    struct virtio_device *device,
    uint16_t notify_offset,
    uint16_t queue_index
) {
    volatile uint16_t *address;

    address = (volatile uint16_t *)(device->notify_base +
                                    ((uint64_t)notify_offset *
                                     (uint64_t)device->notify_multiplier));
    *address = queue_index;
}
