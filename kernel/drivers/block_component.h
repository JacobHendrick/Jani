#ifndef JANI_KERNEL_DRIVERS_BLOCK_COMPONENT_H
#define JANI_KERNEL_DRIVERS_BLOCK_COMPONENT_H

#include "../wasm/component.h"
int block_component_start(void);
int block_component_restart(struct component *caller);
int block_component_request(struct component *caller, uint8_t *bytes, uint32_t length);
int block_component_complete(struct component *caller, int32_t status, const uint8_t *bytes, uint32_t length);
int block_component_dma(struct component *caller, int32_t slot, uint32_t offset,
                         uint8_t *bytes, uint32_t length, int write);
int block_component_submit(struct component *caller, int32_t slot, const uint8_t *bytes, uint32_t length);
int block_component_crash_test(void);
int virtio_blk_dma(uint32_t offset, uint8_t *bytes, uint32_t length, int write);
int virtio_blk_submit(const uint8_t *descriptors, uint32_t length, uint32_t operation, uint64_t sector);
int block_descriptors_validate(const uint8_t *bytes, size_t length, uint32_t operation);

#endif
