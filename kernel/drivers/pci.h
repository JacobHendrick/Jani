#ifndef JANI_KERNEL_DRIVERS_PCI_H
#define JANI_KERNEL_DRIVERS_PCI_H

#include <stdint.h>

#define PCI_VENDOR_VIRTIO 0x1AF4u
#define PCI_DEVICE_VIRTIO_BLK_MODERN 0x1042u
#define PCI_DEVICE_VIRTIO_BLK_TRANSITIONAL 0x1001u

#define PCI_CAPABILITY_ID_VENDOR 0x09u

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
};

uint32_t pci_config_read32(const struct pci_device *device, uint8_t offset);
uint16_t pci_config_read16(const struct pci_device *device, uint8_t offset);
uint8_t pci_config_read8(const struct pci_device *device, uint8_t offset);
void pci_config_write32(const struct pci_device *device, uint8_t offset,
                        uint32_t value);

int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *out);
void pci_enable_bus_master(const struct pci_device *device);
uint64_t pci_bar_address(const struct pci_device *device, uint8_t index);
uint8_t pci_capability_find(const struct pci_device *device,
                            uint8_t capability_id, uint8_t after_offset);

#endif
