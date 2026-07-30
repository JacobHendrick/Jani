#include <stdint.h>

#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "virtio_blk.h"
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

static void prepare_header(uint32_t type, uint64_t sector) {
    struct virtio_blk_req_header *header;

    header = (struct virtio_blk_req_header *)(dma_virtual + DMA_HEADER_OFFSET);
    header->type = type;
    header->reserved = 0;
    header->sector = sector;
    dma_virtual[DMA_STATUS_OFFSET] = 0xFFu;
}

static void fill_header_buffer(struct virtqueue_buffer *buffer) {
    buffer->physical_address = dma_physical + DMA_HEADER_OFFSET;
    buffer->length = (uint32_t)sizeof(struct virtio_blk_req_header);
    buffer->device_writable = 0;
}

static void fill_status_buffer(struct virtqueue_buffer *buffer) {
    buffer->physical_address = dma_physical + DMA_STATUS_OFFSET;
    buffer->length = 1u;
    buffer->device_writable = 1;
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
    return 1;
}

uint64_t virtio_blk_capacity_sectors(void) {
    return blk_capacity;
}

int virtio_blk_io_read(void *context, uint64_t sector, uint8_t *buffer) {
    struct virtqueue_buffer parts[3];

    (void)context;

    if (!blk_ready || (buffer == 0) || (sector >= blk_capacity)) {
        return 0;
    }

    prepare_header(VIRTIO_BLK_T_IN, sector);

    fill_header_buffer(&parts[0]);
    parts[1].physical_address = dma_physical + DMA_DATA_OFFSET;
    parts[1].length = VIRTIO_BLK_SECTOR_SIZE;
    parts[1].device_writable = 1;
    fill_status_buffer(&parts[2]);

    if (!virtqueue_submit(&blk_queue, parts, 3)) {
        return 0;
    }
    if (dma_virtual[DMA_STATUS_OFFSET] != VIRTIO_BLK_S_OK) {
        return 0;
    }

    memcpy(buffer, dma_virtual + DMA_DATA_OFFSET, VIRTIO_BLK_SECTOR_SIZE);
    return 1;
}

int virtio_blk_io_write(void *context, uint64_t sector, const uint8_t *buffer) {
    struct virtqueue_buffer parts[3];

    (void)context;

    if (!blk_ready || (buffer == 0) || (sector >= blk_capacity)) {
        return 0;
    }

    memcpy(dma_virtual + DMA_DATA_OFFSET, buffer, VIRTIO_BLK_SECTOR_SIZE);
    prepare_header(VIRTIO_BLK_T_OUT, sector);

    fill_header_buffer(&parts[0]);
    parts[1].physical_address = dma_physical + DMA_DATA_OFFSET;
    parts[1].length = VIRTIO_BLK_SECTOR_SIZE;
    parts[1].device_writable = 0;
    fill_status_buffer(&parts[2]);

    if (!virtqueue_submit(&blk_queue, parts, 3)) {
        return 0;
    }

    return dma_virtual[DMA_STATUS_OFFSET] == VIRTIO_BLK_S_OK;
}

int virtio_blk_io_flush(void *context) {
    struct virtqueue_buffer parts[2];

    (void)context;

    if (!blk_ready) {
        return 0;
    }

    prepare_header(VIRTIO_BLK_T_FLUSH, 0);

    fill_header_buffer(&parts[0]);
    fill_status_buffer(&parts[1]);

    if (!virtqueue_submit(&blk_queue, parts, 2)) {
        return 0;
    }

    return dma_virtual[DMA_STATUS_OFFSET] == VIRTIO_BLK_S_OK;
}
