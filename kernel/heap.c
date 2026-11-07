#include <stdint.h>
#include <stddef.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

#define KHEAP_BASE 0xffff800000000000ull
#define ALIGN16(x) (((x) + 15) & ~(size_t)15)
#define HDR_MAGIC 0x4a424f54u
#define MIN_BLOCK 16

typedef struct block_hdr {
    size_t size;
    struct block_hdr *next;
    uint32_t free;
    uint32_t magic;
} block_hdr;

#define HDR_SIZE ALIGN16(sizeof(block_hdr))

static block_hdr *head;
static uint64_t heap_end;
static uint64_t heap_total;

static block_hdr *grow(size_t bytes)
{
    size_t need = HDR_SIZE + ALIGN16(bytes);
    size_t pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t base = heap_end;
    for (size_t i = 0; i < pages; i++) {
        void *frame = pmm_alloc_page();
        if (!frame) {
            /* Roll back the pages already allocated for this growth so
               they are not leaked while the rest of the kernel keeps
               running in an OOM situation. */
            for (size_t k = 0; k < i; k++) {
                uint64_t phys = vmm_phys_of(base + k * PAGE_SIZE);
                if (phys)
                    pmm_free_page((void *)phys);
            }
            return NULL;
        }
        vmm_map_page(base + i * PAGE_SIZE, (uint64_t)(uintptr_t)frame,
                     KERNEL_PTE_FLAGS);
    }

    block_hdr *b = (block_hdr *)(uintptr_t)base;
    b->size = pages * PAGE_SIZE - HDR_SIZE;
    b->next = NULL;
    b->free = 1;
    b->magic = HDR_MAGIC;

    heap_end = base + pages * PAGE_SIZE;
    heap_total += pages * PAGE_SIZE;

    if (!head) {
        head = b;
    } else {
        block_hdr *cur = head;
        while (cur->next)
            cur = cur->next;
        cur->next = b;
    }
    return b;
}

void heap_init(void)
{
    head = NULL;
    heap_end = KHEAP_BASE;
    heap_total = 0;
}

static void split(block_hdr *b, size_t bytes)
{
    size_t wanted = ALIGN16(bytes);
    if (b->size < wanted + HDR_SIZE + MIN_BLOCK)
        return;

    block_hdr *n =
        (block_hdr *)((uint8_t *)b + HDR_SIZE + wanted);
    n->size = b->size - wanted - HDR_SIZE;
    n->next = b->next;
    n->free = 1;
    n->magic = HDR_MAGIC;
    b->size = wanted;
    b->next = n;
}

void *kmalloc(size_t size)
{
    /* Reject 0 and sizes whose ALIGN16 rounding would wrap around. */
    if (size == 0 || size > (size_t)-1 - 15)
        return NULL;
    size_t wanted = ALIGN16(size);

    for (block_hdr *b = head; b; b = b->next) {
        if (b->free && b->size >= wanted) {
            split(b, size);
            b->free = 0;
            return (uint8_t *)b + HDR_SIZE;
        }
    }

    block_hdr *b = grow(size);
    if (!b)
        return NULL;
    split(b, size);
    b->free = 0;
    return (uint8_t *)b + HDR_SIZE;
}

static void coalesce(void)
{
    for (block_hdr *b = head; b && b->next; b = b->next) {
        if (b->free && b->next->free) {
            b->size += HDR_SIZE + b->next->size;
            b->next = b->next->next;
        }
    }
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    block_hdr *b = (block_hdr *)((uint8_t *)ptr - HDR_SIZE);
    /* Magic check doubles as a double-free detector: after the first
       free the header is poisoned, so a second free of the same pointer
       (or any bogus pointer) is silently rejected instead of corrupting
       the free list through coalesce(). */
    if (b->magic != HDR_MAGIC)
        return;
    b->magic = 0;
    b->free = 1;
    coalesce();
}

void *kcalloc(size_t n, size_t size)
{
    if (n && size > (size_t)-1 / n)
        return NULL;
    size_t total = n * size;
    void *p = kmalloc(total);
    if (!p)
        return NULL;
    uint8_t *b = p;
    for (size_t i = 0; i < ALIGN16(total); i++)
        b[i] = 0;
    return p;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    block_hdr *b = (block_hdr *)((uint8_t *)ptr - HDR_SIZE);
    /* A freed or bogus pointer must not be treated as a live block:
       reading b->size on a poisoned/foreign header could copy garbage
       amounts of memory or corrupt the heap. */
    if (b->magic != HDR_MAGIC)
        return NULL;
    if (b->size >= ALIGN16(size))
        return ptr;

    void *np = kmalloc(size);
    if (!np)
        return NULL;
    uint8_t *dst = np;
    uint8_t *src = ptr;
    for (size_t i = 0; i < b->size; i++)
        dst[i] = src[i];
    kfree(ptr);
    return np;
}

uint64_t heap_bytes(void)
{
    return heap_total;
}
