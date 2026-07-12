#include <stdint.h>

#include "gdt.h"

#define GDT_ENTRY_COUNT 3

struct gdt_descriptor {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed));

static uint64_t gdt_entries[GDT_ENTRY_COUNT];
static struct gdt_descriptor gdt_descriptor;

static uint64_t gdt_make_entry(uint32_t base, uint32_t limit,
                               uint8_t access, uint8_t flags) {
    uint64_t entry = 0;

    entry |= (uint64_t)(limit & 0xFFFF);
    entry |= (uint64_t)(base & 0xFFFFFF) << 16;
    entry |= (uint64_t)access << 40;
    entry |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    entry |= (uint64_t)(flags & 0x0F) << 52;
    entry |= (uint64_t)((base >> 24) & 0xFF) << 56;

    return entry;
}

static void gdt_load(void) {
    __asm__ volatile ("lgdt %0" : : "m"(gdt_descriptor) : "memory");

    __asm__ volatile (
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        :
        :
        : "rax", "memory"
    );
}

void gdt_init(void) {
    gdt_entries[0] = 0;
    gdt_entries[1] = gdt_make_entry(0, 0xFFFFF, 0x9A, 0xA);
    gdt_entries[2] = gdt_make_entry(0, 0xFFFFF, 0x92, 0xC);

    gdt_descriptor.size = sizeof(gdt_entries) - 1;
    gdt_descriptor.offset = (uint64_t)(uintptr_t)gdt_entries;

    gdt_load();
}
