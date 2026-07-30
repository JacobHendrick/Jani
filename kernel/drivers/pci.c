#include <stdint.h>

#include "../arch/io.h"
#include "pci.h"

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu

#define PCI_OFFSET_VENDOR_ID 0x00u
#define PCI_OFFSET_COMMAND 0x04u
#define PCI_OFFSET_STATUS 0x06u
#define PCI_OFFSET_BAR0 0x10u
#define PCI_OFFSET_CAPABILITY_POINTER 0x34u

#define PCI_COMMAND_MEMORY_SPACE (1u << 1)
#define PCI_COMMAND_BUS_MASTER (1u << 2)

#define PCI_STATUS_CAPABILITY_LIST (1u << 4)

#define PCI_BAR_SPACE_IO 1u
#define PCI_BAR_TYPE_MASK 6u
#define PCI_BAR_TYPE_64BIT 4u
#define PCI_BAR_ADDRESS_MASK 0xFFFFFFF0u

#define PCI_CAPABILITY_WALK_LIMIT 48u

static uint32_t config_address(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
) {
    return (1u << 31) |
           ((uint32_t)bus << 16) |
           (((uint32_t)slot & 0x1Fu) << 11) |
           (((uint32_t)function & 0x07u) << 8) |
           ((uint32_t)offset & 0xFCu);
}

static uint32_t raw_read32(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
) {
    outl(PCI_CONFIG_ADDRESS_PORT, config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA_PORT);
}

uint32_t pci_config_read32(const struct pci_device *device, uint8_t offset) {
    return raw_read32(device->bus, device->slot, device->function, offset);
}

uint16_t pci_config_read16(const struct pci_device *device, uint8_t offset) {
    uint32_t value = pci_config_read32(device, offset);

    return (uint16_t)((value >> (((uint32_t)offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_config_read8(const struct pci_device *device, uint8_t offset) {
    uint32_t value = pci_config_read32(device, offset);

    return (uint8_t)((value >> (((uint32_t)offset & 3u) * 8u)) & 0xFFu);
}

void pci_config_write32(
    const struct pci_device *device,
    uint8_t offset,
    uint32_t value
) {
    outl(PCI_CONFIG_ADDRESS_PORT,
         config_address(device->bus, device->slot, device->function, offset));
    outl(PCI_CONFIG_DATA_PORT, value);
}

int pci_find_device(
    uint16_t vendor_id,
    uint16_t device_id,
    struct pci_device *out
) {
    uint32_t bus;
    uint32_t slot;
    uint32_t function;

    if (out == 0) {
        return 0;
    }

    for (bus = 0; bus < 256u; bus++) {
        for (slot = 0; slot < 32u; slot++) {
            for (function = 0; function < 8u; function++) {
                uint32_t identity = raw_read32((uint8_t)bus, (uint8_t)slot,
                                               (uint8_t)function,
                                               PCI_OFFSET_VENDOR_ID);

                if ((identity & 0xFFFFu) != (uint32_t)vendor_id) {
                    continue;
                }
                if (((identity >> 16) & 0xFFFFu) != (uint32_t)device_id) {
                    continue;
                }

                out->bus = (uint8_t)bus;
                out->slot = (uint8_t)slot;
                out->function = (uint8_t)function;
                out->vendor_id = vendor_id;
                out->device_id = device_id;
                return 1;
            }
        }
    }

    return 0;
}

void pci_enable_bus_master(const struct pci_device *device) {
    uint32_t command;

    command = pci_config_read32(device, PCI_OFFSET_COMMAND) & 0xFFFFu;
    command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    pci_config_write32(device, PCI_OFFSET_COMMAND, command);
}

uint64_t pci_bar_address(const struct pci_device *device, uint8_t index) {
    uint32_t low;
    uint32_t high;

    if (index > 5u) {
        return 0;
    }

    low = pci_config_read32(device,
                            (uint8_t)(PCI_OFFSET_BAR0 + ((uint32_t)index * 4u)));
    if ((low & PCI_BAR_SPACE_IO) != 0) {
        return 0;
    }

    if ((low & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_64BIT) {
        if (index >= 5u) {
            return 0;
        }
        high = pci_config_read32(
            device,
            (uint8_t)(PCI_OFFSET_BAR0 + (((uint32_t)index + 1u) * 4u))
        );
        return ((uint64_t)high << 32) | (uint64_t)(low & PCI_BAR_ADDRESS_MASK);
    }

    return (uint64_t)(low & PCI_BAR_ADDRESS_MASK);
}

uint8_t pci_capability_find(
    const struct pci_device *device,
    uint8_t capability_id,
    uint8_t after_offset
) {
    uint16_t status;
    uint8_t offset;
    uint32_t steps;

    status = pci_config_read16(device, PCI_OFFSET_STATUS);
    if ((status & PCI_STATUS_CAPABILITY_LIST) == 0) {
        return 0;
    }

    if (after_offset == 0) {
        offset = pci_config_read8(device, PCI_OFFSET_CAPABILITY_POINTER);
    } else {
        offset = pci_config_read8(device, (uint8_t)(after_offset + 1u));
    }
    offset = (uint8_t)(offset & 0xFCu);

    steps = 0;
    while ((offset >= 0x40u) && (steps < PCI_CAPABILITY_WALK_LIMIT)) {
        if (pci_config_read8(device, offset) == capability_id) {
            return offset;
        }
        offset = (uint8_t)(pci_config_read8(device, (uint8_t)(offset + 1u)) &
                           0xFCu);
        steps++;
    }

    return 0;
}
