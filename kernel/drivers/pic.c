#include "../arch/io.h"
#include "pic.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20

#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01

void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t mask1;
    uint8_t mask2;

    mask1 = inb(PIC1_DATA);
    mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, offset1);
    io_wait();
    outb(PIC2_DATA, offset2);
    io_wait();

    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, mask1);
    io_wait();
    outb(PIC2_DATA, mask2);
    io_wait();
}


void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
    io_wait();
}

void pic_mask_all(void) {
    outb(PIC1_DATA, 0xFF);
    io_wait();
    outb(PIC2_DATA, 0xFF);
    io_wait();
}

void pic_unmask_irq(uint8_t irq) {
    uint8_t mask;

    if (irq < 8) {
        mask = inb(PIC1_DATA);
        mask &= ~(1 << irq);
        outb(PIC1_DATA, mask);
        io_wait();
        return;
}

    irq = (uint8_t)(irq - 8);
    mask = inb(PIC2_DATA);
    mask &= (uint8_t)~(1 << irq);
    outb(PIC2_DATA, mask);
    io_wait();
}