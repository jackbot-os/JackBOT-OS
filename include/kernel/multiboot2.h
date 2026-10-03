#ifndef JACKBOT_MULTIBOOT2_H
#define JACKBOT_MULTIBOOT2_H

#include <stdint.h>

#define MULTIBOOT2_MAGIC 0x36d76289

struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

#define MB2_TAG_END 0
#define MB2_TAG_CMDLINE 1
#define MB2_TAG_BOOT_LOADER_NAME 2
#define MB2_TAG_MODULE 3
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_MMAP 6
#define MB2_TAG_FRAMEBUFFER 8

struct mb2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t mmap_type;
    uint32_t reserved;
};

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct mb2_mmap_entry entries[];
};

#define MB2_MEMORY_AVAILABLE 1
#define MB2_MEMORY_RESERVED 2
#define MB2_MEMORY_ACPI_RECLAIM 3
#define MB2_MEMORY_ACPI_NVS 4
#define MB2_MEMORY_BAD 5

static inline struct mb2_tag *mb2_first_tag(struct mb2_info *info)
{
    return (struct mb2_tag *)((uint8_t *)info + 8);
}

static inline struct mb2_tag *mb2_next_tag(struct mb2_tag *tag)
{
    uintptr_t addr = (uintptr_t)tag + ((tag->size + 7) & ~7u);
    return (struct mb2_tag *)addr;
}

#endif
