#include <stdint.h>
#include <stddef.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/proc.h>
#include "../build/ustest.h"

#define USER_BASE 0x8000000000ull
#define USER_STACK_VADDR (USER_BASE + 0x100000)
#define USER_STACK_TOP (USER_STACK_VADDR + 0x1000)

extern uint64_t p4_table[512];

static void zero_frame(void *frame)
{
    uint8_t *p = frame;
    for (int i = 0; i < 4096; i++)
        p[i] = 0;
}

thread_t *spawn_test_user(void)
{
    void *p4f = pmm_alloc_page_below(0x40000000);
    uint64_t *p4 = p4f;
    for (int i = 0; i < 512; i++)
        p4[i] = p4_table[i];
    uint64_t p4phys = (uint64_t)(uintptr_t)p4f;

    uint64_t len = ustest_bin_len;
    uint64_t pages = (len + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++) {
        void *frame = pmm_alloc_page_below(0x40000000);
        uint8_t *dst = frame;
        uint64_t off = p * 4096;
        uint64_t n = len - off;
        if (n > 4096)
            n = 4096;
        for (uint64_t k = 0; k < n; k++)
            dst[k] = ustest_bin[off + k];
        for (uint64_t k = n; k < 4096; k++)
            dst[k] = 0;
        vmm_map_in(p4phys, USER_BASE + off, (uint64_t)(uintptr_t)frame,
                   PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    void *sf = pmm_alloc_page_below(0x40000000);
    zero_frame(sf);
    vmm_map_in(p4phys, USER_STACK_VADDR, (uint64_t)(uintptr_t)sf,
               PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    return thread_create_user(p4phys, USER_BASE, USER_STACK_TOP);
}
