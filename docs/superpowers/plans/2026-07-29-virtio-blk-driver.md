# virtio-blk: the store's first real disk

**Goal:** back `struct object_store_io` with a virtio-blk device so the Phase 2
milestone demo runs in QEMU — create objects, write versions, kill QEMU
mid-write, reboot, recover with all checksums valid; snapshot, mutate, roll
back, show the old state.

**Who writes this:** Jacob. This is the first driver, and per the working
agreement the first instance of a pattern sets the house style. Support work produced
this plan and will write the hosted virtqueue harness and Makefile wiring once
the signatures below are settled, then review.

**Status: IMPLEMENTED 2026-07-29.** Both decisions below were resolved as
modern + polled, and all six stages are written and verified in QEMU (format,
put, snapshot, mutate, rollback, and a clean remount on a second boot). Support work produced it at Jacob's direction, waiving the first-driver rule for this one; it
still wants review and hardware verification. The prose below is kept as the
design record. One thing the plan did not predict: the kernel had never
enabled SSE, and the first struct-by-value call into the store took a #UD --
see the devlog entry.

## What already exists

- `struct object_store_io` — `{ context, sector_count, read_sector,
  write_sector, flush }`. The integration surface is already defined and
  hosted-tested against two fake disks. Nothing about the store needs to
  change; a virtio-blk backend just fills in these five fields.
- `pmm_alloc_frame()` returns a physical frame address, and
  `vmm_physical_to_virtual()` maps it through the HHDM. Together these are
  exactly what virtqueue memory needs.
- `vmm_virtual_to_physical()` for translating buffers before handing them to
  the device.

## What is missing

1. 32-bit port I/O. `kernel/arch/io.h` has `outb`/`inb`/`outw` but no
   `outl`/`inl`, and PCI configuration space is a 32-bit port protocol.
2. PCI enumeration — nothing exists under `kernel/drivers/`.
3. virtio transport, virtqueue mechanics, virtio-blk request layer.
4. The `object_store_io` adapter and a `sector_count` derived from device
   capacity.

## Two decisions to make before writing code

Both are house-style calls, which is why they are yours and not assumptions
baked into this plan.

**Legacy or modern virtio.** QEMU's `virtio-blk-pci` speaks modern (virtio
1.0+) by default and legacy with `disable-modern=on`. Legacy is a single I/O
BAR and about a dozen port accesses — you could have it reading sectors in an
evening. Modern requires walking the PCI capability list to find structures
inside MMIO BARs. The recommendation is **modern**: the blueprint says v1.1+,
legacy is deprecated, and the capability walk is the same shape NVMe needs in
Phase 4, so it pays twice. Legacy-first as a learning ramp is defensible if
you throw it away afterward rather than keeping both paths.

**Polling or interrupts.** `read_sector` and `write_sector` are synchronous and
return `int`, so a polled used-ring loop matches the API with no completion
bookkeeping. The recommendation is **poll**. Interrupts and MSI-X belong with
the Phase 4 driver-component model, where the interrupt-to-message bridge
exists.

## Build order

Each stage ends at something you can see over serial, so a failure is localized
to the stage that produced it.

### Stage 0 — 32-bit port I/O

Add to `kernel/arch/io.h`, matching the existing inline style:

```c
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
```

### Stage 1 — PCI enumeration (`kernel/drivers/pci.{c,h}`)

Config space is address port `0xCF8`, data port `0xCFC`. The address is
`(1u << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC)`.

Proposed surface — change names freely, this sets the convention:

```c
struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar[6];
};

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint32_t value);
int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *out);
void pci_enable_bus_master(const struct pci_device *device);
```

A brute-force scan over bus 0-255, device 0-31, function 0-7 is fine — no
bridge recursion needed for QEMU's flat topology. Vendor `0xFFFF` means the
slot is empty.

virtio-blk is vendor `0x1AF4`, device `0x1001` (legacy) or `0x1042` (modern).

Two things that bite: BAR sizing requires writing all-ones, reading back, and
restoring the original value — do it with interrupts off. And **bus mastering
is off by default**; set bit 2 of the command register at offset `0x04` or the
device will never touch your rings and you will debug a silent hang.

*Verify:* boot QEMU with `-drive file=disk.img,if=none,id=d0 -device
virtio-blk-pci,drive=d0` and print the device's bus/slot/function, IDs, and
decoded BARs over serial.

### Stage 2 — virtio transport (`kernel/drivers/virtio.{c,h}`)

Walk the PCI capability list from offset `0x34`, looking for capability ID
`0x09` (vendor-specific). Each virtio capability carries a `cfg_type`:

| cfg_type | Structure |
|---|---|
| 1 | common configuration |
| 2 | notify |
| 3 | ISR status |
| 4 | device-specific configuration |

Each names a BAR index plus an offset and length within it. Map those through
the HHDM and keep pointers to them.

Bring-up sequence, in order — the device enforces it:

1. Write 0 to status (reset), then read back until it reads 0.
2. Status |= `ACKNOWLEDGE` (1).
3. Status |= `DRIVER` (2).
4. Read feature bits, select the subset you support. **You must accept
   `VIRTIO_F_VERSION_1` (bit 32)** for a modern device. Negotiate
   `VIRTIO_BLK_F_FLUSH` (bit 9) — see the durability note below.
5. Status |= `FEATURES_OK` (8), then **read status back**. If `FEATURES_OK`
   cleared, the device rejected your set and you must abort rather than
   proceed.
6. Set up queues (stage 3).
7. Status |= `DRIVER_OK` (4).

*Verify:* status reads back `DRIVER_OK`, and the device-configuration
structure reports a plausible capacity.

### Stage 3 — virtqueue (`kernel/drivers/virtqueue.{c,h}`)

Three regions. Under virtio 1.0 they may be separate physical allocations, so
one `pmm_alloc_frame()` each is fine and avoids the legacy layout's contiguity
and alignment rules.

```c
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};
```

Descriptor flags: `NEXT` = 1 (chain continues), `WRITE` = 2 (device writes to
this buffer, i.e. it is device-writable).

The submit path: fill a descriptor chain, write its head index into
`avail->ring[avail->idx % queue_size]`, barrier, increment `avail->idx`,
barrier, write the queue index to the notify address. Then poll `used->idx`
until it moves, and read the completion out of `used->ring`.

### Stage 4 — virtio-blk requests (`kernel/drivers/virtio_blk.{c,h}`)

A request is always a three-descriptor chain:

```c
struct virtio_blk_req_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};
```

| Descriptor | Contents | Direction |
|---|---|---|
| 0 | the 16-byte header | device-readable |
| 1 | the 512-byte data buffer | readable for writes, **writable** for reads |
| 2 | a single status byte | device-writable |

Types: `IN` = 0 (read), `OUT` = 1 (write), `FLUSH` = 4. Status: 0 = OK,
1 = IOERR, 2 = UNSUPP. A flush request carries a header and status byte with no
data descriptor.

Target surface:

```c
int virtio_blk_read(uint64_t sector, uint8_t *buffer);
int virtio_blk_write(uint64_t sector, const uint8_t *buffer);
int virtio_blk_flush(void);
uint64_t virtio_blk_capacity_sectors(void);
```

### Stage 5 — the adapter

Fill in an `object_store_io` whose callbacks forward to stage 4 and whose
`sector_count` comes from `virtio_blk_capacity_sectors()`. This should be
about twenty lines; if it is more, something leaked out of stage 4.

### Stage 6 — the milestone demo in `main.c`

Format, put objects, get them back, snapshot, mutate, roll back. Then the real
test: `kill -9` QEMU mid-write and confirm the next boot mounts and recovers.

## Five traps

**1. Flush must actually flush. This is the one that matters.** The TLA+ model
proves the commit protocol is crash-safe *on the assumption that flush is a
real durability barrier*. If `virtio_blk_flush()` returns 1 without issuing a
`VIRTIO_BLK_T_FLUSH` request and waiting for its completion, every crash-safety
property in the model becomes fiction, and not one hosted test will notice —
they all use a fake disk where flush is a no-op that returns success. Negotiate
`VIRTIO_BLK_F_FLUSH`; if the device does not offer it, fail loudly at mount
rather than silently running without durability.

**2. Descriptors carry physical addresses.** Every buffer handed to the device
must go through `vmm_virtual_to_physical()`, and a buffer that crosses a page
boundary needs splitting into multiple descriptors because physical contiguity
does not follow virtual contiguity.

**3. The store hands you arbitrary kernel pointers.** `read_sector(ctx, sector,
buffer)` gets whatever the caller had — a stack array, heap memory, an interior
pointer into the arena. A 512-byte stack buffer can straddle a page boundary.
The safe default is bouncing through one dedicated `pmm_alloc_frame()` DMA
buffer per operation and `memcpy`ing; it costs a copy per sector and removes an
entire class of bug. Optimize later, with tests.

**4. Barriers.** At minimum a compiler barrier (`__asm__ volatile("" ::: "memory")`)
before incrementing `avail->idx` and before the notify write. x86 is
store-ordered so you will get away with less than the spec requires — which is
precisely the danger, because it will work in QEMU and fail elsewhere. Write
what the spec says, not what QEMU tolerates.

**5. Capacity is in 512-byte units** regardless of the device's logical block
size. `OBJECT_STORE_SECTOR_SIZE` is also 512, so they line up today. Assert it
rather than assuming it, so a 4Kn device fails at mount instead of corrupting
silently.

## Verification

`make test` and `make kernel` must stay green throughout — the store code
itself does not change, so any hosted regression means a stage leaked into it.

This requires manual verification. Every stage needs you to run it, and QEMU
agreeing is not the same as hardware agreeing.
