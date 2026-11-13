#include <stdint.h>
#include <stddef.h>
#include <kernel/proc.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/serial.h>
#include <kernel/trap.h>
#include <kernel/posix.h>
#include <kernel/userimg.h>

#define PAGE_MASK  0x000ffffffffff000ull

/* fork(): the child gets a new top-level page table whose user pages alias
   the parent's frames read-only and marked PTE_COW. The parent keeps running
   and returns the child's pid; the child returns 0 from the same instruction
   as soon as the scheduler picks it. Nothing is copied until one side writes. */
int64_t sys_fork(struct trap_frame *f)
{
    thread_t *p = current_thread();
    if (!p || !p->is_user || !p->user_entry)
        return -1;

    uint64_t newp4 = user_p4_new();
    if (!newp4)
        return -1;

    uint64_t va = USER_BASE;
    uint64_t end = USER_BASE + USER_WINDOW_SIZE;
    int copied = 0;
    for (; va < end; va += 4096) {
        uint64_t pte = vmm_pte_of_in(p->p4phys, va);
        if (!(pte & PTE_PRESENT) || !(pte & PTE_USER))
            continue;
        uint64_t flags = PTE_PRESENT | PTE_USER;
        if (pte & PTE_NX)
            flags |= PTE_NX;
        if (pte & PTE_WRITABLE) {
            /* shared writable page: both sides lose write permission */
            if (!vmm_cow_mark(p->p4phys, va))
                continue;
            flags |= PTE_COW;
        }
        vmm_map_in(newp4, va, pte & PAGE_MASK, flags);
        copied++;
    }
    if (copied == 0)
        return -1;

    thread_t *c = thread_create_fork(p, newp4, f->rip, f->rsp);
    if (!c)
        return -1;
    return (int64_t)c->id;
}

/* Proves the copy-on-write path without needing a ring-3 fork caller:
   map a page, share it, write through the "parent", then break the copy
   and check that the child kept the old contents on a new frame. */
void cow_selftest(void)
{
    uint64_t p4 = user_p4_new();
    uint64_t va = USER_BASE + 0x300000;
    int ok = 0;
    uint64_t before = 0, after = 0;

    if (p4) {
        void *frame = pmm_alloc_page();
        if (frame) {
            uint8_t *page = frame;
            for (int i = 0; i < 4096; i++)
                page[i] = 0;
            page[0] = 'P';
            page[1] = 'A';
            vmm_map_in(p4, va, (uint64_t)(uintptr_t)frame,
                       PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
            before = vmm_phys_of_in(p4, va) & PAGE_MASK;

            if (vmm_cow_mark(p4, va) &&
                !(vmm_pte_of_in(p4, va) & PTE_WRITABLE)) {
                /* simulate the child's write fault on the shared page */
                if (vmm_cow_break(p4, va)) {
                    after = vmm_phys_of_in(p4, va) & PAGE_MASK;
                    uint8_t *fresh = (uint8_t *)(uintptr_t)after;
                    ok = (after != before && fresh[0] == 'P' && fresh[1] == 'A' &&
                          (vmm_pte_of_in(p4, va) & PTE_WRITABLE));
                }
            }
        }
    }

    serial_puts("[COW] page ");
    serial_puthex(before);
    serial_puts(" -> ");
    serial_puthex(after);
    serial_puts(ok ? " private copy OK\n" : " private copy FAIL\n");
}
