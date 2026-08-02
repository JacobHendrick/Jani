#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#include "../../arch/stack.h"
#include "../../drivers/pit.h"
#include "../../mm/heap.h"

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{
}

void *
os_malloc(unsigned size)
{
    return kmalloc((size_t)size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return krealloc(ptr, (size_t)size);
}

void
os_free(void *ptr)
{
    kfree(ptr);
}

int
os_printf(const char *format, ...)
{
    va_list args;
    int written;

    va_start(args, format);
    written = jani_vprintf(format, args);
    va_end(args);

    return written;
}

int
os_vprintf(const char *format, va_list ap)
{
    return jani_vprintf(format, ap);
}

uint64
os_time_get_boot_us(void)
{
    return pit_get_microseconds();
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

korp_tid
os_self_thread(void)
{
    return 1;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    return (uint8 *)kernel_stack_limit();
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

int
os_mutex_init(korp_mutex *mutex)
{
    mutex->locked = 0;
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    mutex->locked = 0;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    mutex->locked = 1;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    mutex->locked = 0;
    return BHT_OK;
}

int
os_cond_init(korp_cond *cond)
{
    cond->waiters = 0;
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    cond->waiters = 0;
    return BHT_OK;
}

int
os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
    (void)cond;
    (void)mutex;
    return BHT_ERROR;
}

int
os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{
    (void)cond;
    (void)mutex;
    (void)useconds;
    return BHT_ERROR;
}

int
os_cond_signal(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_cond_broadcast(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *memory;

    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;

    if (size == 0) {
        return NULL;
    }

    memory = kmalloc(size);

    if (memory == NULL) {
        return NULL;
    }

    memset(memory, 0, size);

    return memory;
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    kfree(addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;

    if ((prot & MMAP_PROT_EXEC) != 0) {
        return -1;
    }

    if ((prot & (MMAP_PROT_READ | MMAP_PROT_WRITE))
        != (MMAP_PROT_READ | MMAP_PROT_WRITE)) {
        return -1;
    }

    return 0;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    void *memory;

    if ((new_size == 0) || (new_size < old_size)) {
        return NULL;
    }

    memory = kmalloc(new_size);

    if (memory == NULL) {
        return NULL;
    }

    if ((old_addr != NULL) && (old_size > 0)) {
        memcpy(memory, old_addr, old_size);
    }

    memset((uint8 *)memory + old_size, 0, new_size - old_size);

    if (old_addr != NULL) {
        kfree(old_addr);
    }

    return memory;
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return -1;
}

void
os_dcache_flush(void)
{
}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}
