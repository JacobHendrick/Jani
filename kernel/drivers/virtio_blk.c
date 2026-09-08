#include <stdint.h>

#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "virtio_blk.h"
#include "block_component.h"
#include "virtio_pci.h"
#include "virtqueue.h"

#define DMA_HEADER_OFFSET 0u
#define DMA_STATUS_OFFSET 16u
#define DMA_DATA_OFFSET 512u

#define DEVICE_CFG_CAPACITY_OFFSET 0u

static struct virtio_device blk_device;
static struct virtqueue blk_queue;
static uint64_t dma_physical;
static uint8_t *dma_virtual;
static uint64_t blk_capacity;
static int blk_ready;

static int attach_any_virtio_blk(void) {
    if (virtio_pci_attach(&blk_device, PCI_DEVICE_VIRTIO_BLK_MODERN)) {
        return 1;
    }
    return virtio_pci_attach(&blk_device, PCI_DEVICE_VIRTIO_BLK_TRANSITIONAL);
}

static int allocate_dma_page(void) {
    void *virtual_address;

    dma_physical = pmm_alloc_frame();
    if (dma_physical == 0) {
        return 0;
    }

    virtual_address = vmm_physical_to_virtual(dma_physical);
    if (virtual_address == 0) {
        pmm_free_frame(dma_physical);
        dma_physical = 0;
        return 0;
    }

    dma_virtual = virtual_address;
    memset(dma_virtual, 0, PMM_FRAME_SIZE);
    return 1;
}

int virtio_blk_init(void) {
    uint64_t wanted;
    uint64_t accepted;

    blk_ready = 0;
    blk_capacity = 0;

    if (!attach_any_virtio_blk()) {
        kputs("ERROR: no virtio-blk device found on the PCI bus\n");
        return 0;
    }

    if (!virtio_pci_reset(&blk_device)) {
        kputs("ERROR: virtio-blk did not acknowledge reset\n");
        return 0;
    }

    virtio_pci_add_status(&blk_device, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_pci_add_status(&blk_device, VIRTIO_STATUS_DRIVER);

    wanted = (1ULL << VIRTIO_FEATURE_VERSION_1) |
             (1ULL << VIRTIO_BLK_FEATURE_FLUSH);
    accepted = 0;
    if (!virtio_pci_negotiate(&blk_device, wanted, &accepted)) {
        virtio_pci_add_status(&blk_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-blk feature negotiation failed\n");
        return 0;
    }

    if ((accepted & (1ULL << VIRTIO_BLK_FEATURE_FLUSH)) == 0) {
        virtio_pci_add_status(&blk_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-blk has no FLUSH; refusing to mount a store "
              "whose crash safety depends on it\n");
        return 0;
    }

    if (!virtqueue_setup(&blk_queue, &blk_device, 0)) {
        virtio_pci_add_status(&blk_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-blk request queue setup failed\n");
        return 0;
    }

    if (!allocate_dma_page()) {
        virtio_pci_add_status(&blk_device, VIRTIO_STATUS_FAILED);
        kputs("ERROR: virtio-blk could not allocate its DMA page\n");
        return 0;
    }

    virtio_pci_add_status(&blk_device, VIRTIO_STATUS_DRIVER_OK);

    blk_capacity = *(volatile uint64_t *)(blk_device.device_cfg +
                                          DEVICE_CFG_CAPACITY_OFFSET);
    blk_ready = 1;

    printk("virtio-blk ready: %d sectors of 512 bytes\n", (int)blk_capacity);
    return block_component_start();
}

uint64_t virtio_blk_capacity_sectors(void) {
    return blk_capacity;
}


int virtio_blk_dma(uint32_t offset, uint8_t *bytes, uint32_t length, int write) {
    if (!blk_ready || offset > PMM_FRAME_SIZE || length > PMM_FRAME_SIZE - offset ||
        (length != 0 && bytes == NULL)) return 0;
    if (length) {
        if (write) memcpy(dma_virtual + offset, bytes, length);
        else memcpy(bytes, dma_virtual + offset, length);
    }
    return 1;
}

int virtio_blk_submit(const uint8_t *descriptors, uint32_t length, uint32_t operation, uint64_t sector) {
    struct virtqueue_buffer buffers[3];
    struct virtio_blk_req_header header;
    if (!blk_ready || !block_descriptors_validate(descriptors, length, operation)) return 0;
    memcpy(&header, dma_virtual, sizeof(header));
    if (header.type != operation || header.reserved != 0 || header.sector != sector) return 0;
    for (uint32_t i = 0; i < length / 12; i++) {
        uint32_t descriptor[3];
        memcpy(descriptor, descriptors + i * 12, sizeof(descriptor));
        buffers[i].physical_address = dma_physical + descriptor[0];
        buffers[i].length = descriptor[1];
        buffers[i].device_writable = (int)descriptor[2];
    }
    if (!virtqueue_submit(&blk_queue, buffers, (uint16_t)(length / 12))) {
        /* A timed-out device might still DMA. Do not reuse its ring or buffers. */
        blk_ready = 0;
        return 0;
    }
    return 1;
}
