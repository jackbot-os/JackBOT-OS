#include <stdint.h>
#include <stddef.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/serial.h>

#define IDENTITY_LIMIT 0x40000000ull
#define ADDR_MASK 0x000ffffffffff000ull

extern uint64_t p4_table[512];
#define P4_VIRT p4_table

static void vmm_oom_halt(uint64_t virt)
{
    serial_puts("[VMM] FATAL: out of page-table memory mapping ");
    serial_puthex(virt);
    serial_puts("\n");
    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

static uint64_t *next_table(uint64_t *parent, uint64_t index)
{
    if (parent[index] & PTE_PRESENT)
        return (uint64_t *)(uintptr_t)(parent[index] & ADDR_MASK);

    void *frame = pmm_alloc_page_below(IDENTITY_LIMIT);
    if (!frame)
        return NULL;
    uint64_t *table = (uint64_t *)frame;
    for (int i = 0; i < 512; i++)
        table[i] = 0;
    parent[index] = (uint64_t)(uintptr_t)frame | PTE_PRESENT | PTE_WRITABLE;
    return table;
}

static uint64_t *next_table_u(uint64_t *parent, uint64_t index, int user)
{
    if (parent[index] & PTE_PRESENT) {
        if (user)
            parent[index] |= PTE_USER;
        return (uint64_t *)(uintptr_t)(parent[index] & ADDR_MASK);
    }
    void *frame = pmm_alloc_page_below(IDENTITY_LIMIT);
    if (!frame)
        return NULL;
    uint64_t *table = (uint64_t *)frame;
    for (int i = 0; i < 512; i++)
        table[i] = 0;
    parent[index] = (uint64_t)(uintptr_t)frame | PTE_PRESENT | PTE_WRITABLE
                  | (user ? PTE_USER : 0);
    return table;
}

void vmm_map_in(uint64_t p4phys, uint64_t virt, uint64_t phys, uint64_t flags)
{
    int user = (flags & PTE_USER) != 0;
    uint64_t i4 = (virt >> 39) & 0x1ff;
    uint64_t i3 = (virt >> 30) & 0x1ff;
    uint64_t i2 = (virt >> 21) & 0x1ff;
    uint64_t i1 = (virt >> 12) & 0x1ff;

    uint64_t *p4 = (uint64_t *)(uintptr_t)p4phys;
    uint64_t *p3 = next_table_u(p4, i4, user);
    if (!p3)
        vmm_oom_halt(virt);
    uint64_t *p2 = next_table_u(p3, i3, user);
    if (!p2)
        vmm_oom_halt(virt);
    uint64_t *p1 = next_table_u(p2, i2, user);
    if (!p1)
        vmm_oom_halt(virt);
    p1[i1] = (phys & ADDR_MASK) | flags;
}

void vmm_init(void)
{
}

void vmm_invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t i4 = (virt >> 39) & 0x1ff;
    uint64_t i3 = (virt >> 30) & 0x1ff;
    uint64_t i2 = (virt >> 21) & 0x1ff;
    uint64_t i1 = (virt >> 12) & 0x1ff;

    uint64_t *p3 = next_table(P4_VIRT, i4);
    if (!p3)
        vmm_oom_halt(virt);
    uint64_t *p2 = next_table(p3, i3);
    if (!p2)
        vmm_oom_halt(virt);
    uint64_t *p1 = next_table(p2, i2);
    if (!p1)
        vmm_oom_halt(virt);

    p1[i1] = (phys & ADDR_MASK) | flags;
    vmm_invlpg(virt);
}

void vmm_unmap_page(uint64_t virt)
{
    uint64_t i4 = (virt >> 39) & 0x1ff;
    uint64_t i3 = (virt >> 30) & 0x1ff;
    uint64_t i2 = (virt >> 21) & 0x1ff;
    uint64_t i1 = (virt >> 12) & 0x1ff;

    if (!(P4_VIRT[i4] & PTE_PRESENT))
        return;
    uint64_t *p3 = (uint64_t *)(uintptr_t)(P4_VIRT[i4] & ADDR_MASK);
    if (!(p3[i3] & PTE_PRESENT))
        return;
    uint64_t *p2 = (uint64_t *)(uintptr_t)(p3[i3] & ADDR_MASK);
    if (!(p2[i2] & PTE_PRESENT))
        return;
    uint64_t *p1 = (uint64_t *)(uintptr_t)(p2[i2] & ADDR_MASK);

    p1[i1] = 0;
    vmm_invlpg(virt);
}

uint64_t vmm_phys_of(uint64_t virt)
{
    uint64_t i4 = (virt >> 39) & 0x1ff;
    uint64_t i3 = (virt >> 30) & 0x1ff;
    uint64_t i2 = (virt >> 21) & 0x1ff;
    uint64_t i1 = (virt >> 12) & 0x1ff;

    if (!(P4_VIRT[i4] & PTE_PRESENT))
        return 0;
    uint64_t *p3 = (uint64_t *)(uintptr_t)(P4_VIRT[i4] & ADDR_MASK);
    if (!(p3[i3] & PTE_PRESENT))
        return 0;
    uint64_t *p2 = (uint64_t *)(uintptr_t)(p3[i3] & ADDR_MASK);
    if (!(p2[i2] & PTE_PRESENT))
        return 0;
    if (p2[i2] & PTE_HUGE)
        return (p2[i2] & ADDR_MASK) + (virt & 0x1fffff);
    uint64_t *p1 = (uint64_t *)(uintptr_t)(p2[i2] & ADDR_MASK);
    if (!(p1[i1] & PTE_PRESENT))
        return 0;
    return (p1[i1] & ADDR_MASK) + (virt & 0xfff);
}
