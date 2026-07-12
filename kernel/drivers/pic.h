#ifndef JANI_KERNEL_DRIVERS_PIC_H
#define JANI_KERNEL_DRIVERS_PIC_H

#include <stdint.h>

#define PIC1_VECTOR_OFFSET 32
#define PIC2_VECTOR_OFFSET 40

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_send_eoi(uint8_t irq);
void pic_mask_all(void);
void pic_unmask_irq(uint8_t irq);

#endif
