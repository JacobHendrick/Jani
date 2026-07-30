#ifndef JANI_KERNEL_DRIVERS_VIRTIO_BLK_H
#define JANI_KERNEL_DRIVERS_VIRTIO_BLK_H

#include <stdint.h>

#define VIRTIO_BLK_SECTOR_SIZE 512u

#define VIRTIO_BLK_T_IN 0u
#define VIRTIO_BLK_T_OUT 1u
#define VIRTIO_BLK_T_FLUSH 4u

#define VIRTIO_BLK_S_OK 0u

#define VIRTIO_BLK_FEATURE_FLUSH 9u

struct virtio_blk_req_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

_Static_assert(sizeof(struct virtio_blk_req_header) == 16u,
               "virtio_blk_req_header must match the virtio 1.1 layout");

int virtio_blk_init(void);
uint64_t virtio_blk_capacity_sectors(void);

int virtio_blk_io_read(void *context, uint64_t sector, uint8_t *buffer);
int virtio_blk_io_write(void *context, uint64_t sector, const uint8_t *buffer);
int virtio_blk_io_flush(void *context);

#endif
