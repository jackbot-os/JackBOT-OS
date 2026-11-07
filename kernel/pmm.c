#include <stdint.h>
#include <stddef.h>
#include <kernel/pmm.h>
#include <kernel/multiboot2.h>

/* 位图放在内核 BSS 静态区：早期固定 0x400000 会与 GRUB multiboot 模块
   (GRUBMODS 常加载在 0x400000 附近) 重叠并覆盖模块数据。上限 4GB=128KB。 */
#define MAX_PHYS_BYTES (4ull * 1024 * 1024 * 1024)
#define MAX_PFN (MAX_PHYS_BYTES / PAGE_SIZE)
#define LOW_RESERVE_END 0x400000ull

extern uint8_t kernel_end[];

static uint8_t bitmap_storage[MAX_PFN / 8];
static uint8_t *bitmap;
static uint64_t max_pfn;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t hint_pfn;

static inline void bmp_set(uint64_t pfn)
{
    bitmap[pfn >> 3] |= (uint8_t)(1u << (pfn & 7));
}

static inline void bmp_clear(uint64_t pfn)
{
    bitmap[pfn >> 3] &= (uint8_t)~(1u << (pfn & 7));
}

static inline int bmp_test(uint64_t pfn)
{
    return (bitmap[pfn >> 3] >> (pfn & 7)) & 1;
}

void pmm_reserve(uint64_t start, uint64_t end)
{
    uint64_t first = start / PAGE_SIZE;
    uint64_t last = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = first; p < last; p++) {
        if (p < max_pfn && !bmp_test(p)) {
            bmp_set(p);
            free_pages--;
        }
    }
}

static void mark_available(uint64_t start, uint64_t len)
{
    uint64_t first = (start + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t last = (start + len) / PAGE_SIZE;
    if (last > max_pfn)
        last = max_pfn;
    for (uint64_t p = first; p < last; p++) {
        if (bmp_test(p)) {
            bmp_clear(p);
            free_pages++;
        }
    }
}

void pmm_init(uint64_t mb_info)
{
    struct mb2_info *info = (struct mb2_info *)(uintptr_t)mb_info;

    uint64_t highest = 0;
    struct mb2_tag *tag = mb2_first_tag(info);
    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MMAP) {
            struct mb2_tag_mmap *mmap = (struct mb2_tag_mmap *)tag;
            uint8_t *p = (uint8_t *)mmap->entries;
            uint8_t *end = (uint8_t *)mmap + mmap->size;
            while (p < end) {
                struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)p;
                uint64_t region_end = e->addr + e->len;
                if (e->mmap_type == MB2_MEMORY_AVAILABLE &&
                    region_end > highest)
                    highest = region_end;
                p += mmap->entry_size;
            }
        }
        tag = mb2_next_tag(tag);
    }

    max_pfn = (highest + PAGE_SIZE - 1) / PAGE_SIZE;
    if (max_pfn > MAX_PFN)
        max_pfn = MAX_PFN;
    total_pages = max_pfn;

    bitmap = bitmap_storage;
    uint64_t bitmap_bytes = (max_pfn + 7) / 8;
    bitmap_bytes = (bitmap_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t i = 0; i < bitmap_bytes; i++)
        bitmap[i] = 0xff;
    free_pages = 0;

    tag = mb2_first_tag(info);
    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MMAP) {
            struct mb2_tag_mmap *mmap = (struct mb2_tag_mmap *)tag;
            uint8_t *p = (uint8_t *)mmap->entries;
            uint8_t *end = (uint8_t *)mmap + mmap->size;
            while (p < end) {
                struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)p;
                if (e->mmap_type == MB2_MEMORY_AVAILABLE)
                    mark_available(e->addr, e->len);
                p += mmap->entry_size;
            }
        }
        tag = mb2_next_tag(tag);
    }

    /* 保留整个内核镜像（含 .bss）、multiboot info 与早期模块 */
    uint64_t k_end = (uint64_t)(uintptr_t)kernel_end;
    if (k_end < LOW_RESERVE_END)
        k_end = LOW_RESERVE_END;
    pmm_reserve(0, k_end);

    /* 显式保留 multiboot info 结构本身（可能位于 4MB 之后，含 tags） */
    {
        uint64_t mbp = mb_info & ~(PAGE_SIZE - 1);
        pmm_reserve(mbp, mbp + 16384);
    }

    tag = mb2_first_tag(info);
    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MODULE) {
            struct mb2_tag_module *m = (struct mb2_tag_module *)tag;
            if (m->mod_end > m->mod_start)
                pmm_reserve(m->mod_start, m->mod_end);
        }
        tag = mb2_next_tag(tag);
    }

    hint_pfn = (k_end + PAGE_SIZE - 1) / PAGE_SIZE;
}

void *pmm_alloc_page(void)
{
    for (uint64_t p = hint_pfn; p < max_pfn; p++) {
        if (!bmp_test(p)) {
            bmp_set(p);
            free_pages--;
            hint_pfn = p + 1;
            return (void *)(p * PAGE_SIZE);
        }
    }
    for (uint64_t p = 0; p < hint_pfn; p++) {
        if (!bmp_test(p)) {
            bmp_set(p);
            free_pages--;
            return (void *)(p * PAGE_SIZE);
        }
    }
    return NULL;
}

void *pmm_alloc_page_below(uint64_t max_phys)
{
    uint64_t bound = max_phys / PAGE_SIZE;
    if (bound > max_pfn)
        bound = max_pfn;
    for (uint64_t p = hint_pfn < bound ? hint_pfn : 0; p < bound; p++) {
        if (!bmp_test(p)) {
            bmp_set(p);
            free_pages--;
            if (p + 1 > hint_pfn)
                hint_pfn = p + 1;
            return (void *)(p * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free_page(void *phys)
{
    uint64_t pfn = (uint64_t)(uintptr_t)phys / PAGE_SIZE;
    if (pfn >= max_pfn || !bmp_test(pfn))
        return;
    bmp_clear(pfn);
    free_pages++;
    if (pfn < hint_pfn)
        hint_pfn = pfn;
}

uint64_t pmm_total_bytes(void)
{
    return total_pages * PAGE_SIZE;
}

uint64_t pmm_free_bytes(void)
{
    return free_pages * PAGE_SIZE;
}
