#include <stdint.h>
#include <stddef.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/proc.h>
#include "../build/ustest.h"
#include "../build/ucrash.h"

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

static thread_t *spawn_from(const uint8_t *code, uint64_t len)
{
    void *p4f = pmm_alloc_page_below(0x40000000);
    uint64_t *p4 = p4f;
    for (int i = 0; i < 512; i++)
        p4[i] = p4_table[i];
    uint64_t p4phys = (uint64_t)(uintptr_t)p4f;

    uint64_t pages = (len + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++) {
        void *frame = pmm_alloc_page_below(0x40000000);
        uint8_t *dst = frame;
        uint64_t off = p * 4096;
        uint64_t n = len - off;
        if (n > 4096)
            n = 4096;
        for (uint64_t k = 0; k < n; k++)
            dst[k] = code[off + k];
        for (uint64_t k = n; k < 4096; k++)
            dst[k] = 0;
        vmm_map_in(p4phys, USER_BASE + off, (uint64_t)(uintptr_t)frame,
                   PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    void *sf = pmm_alloc_page_below(0x40000000);
    zero_frame(sf);
    vmm_map_in(p4phys, USER_STACK_VADDR, (uint64_t)(uintptr_t)sf,
               PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    thread_t *t = thread_create_user(p4phys, USER_BASE, USER_STACK_TOP);
    if (t) {
        t->mem_bytes = pages * 4096 + 4096;
        if (t->quota_mem_bytes && t->mem_bytes > t->quota_mem_bytes) {
            process_kill(t, "memory quota exceeded at spawn");
            return 0;
        }
    }
    return t;
}

thread_t *spawn_test_user(void)
{
    return spawn_from(ustest_bin, ustest_bin_len);
}

thread_t *spawn_crash_user(void)
{
    return spawn_from(ucrash_bin, ucrash_bin_len);
}

/* Load an arbitrary machine-code image (e.g. a JCAN payload produced by
   tools/jcacc.py) as a ring-3 process at USER_BASE. */
thread_t *spawn_user_image(const uint8_t *code, uint64_t len)
{
    if (!code || len == 0 || len > 0x100000)
        return 0;
    return spawn_from(code, len);
}
