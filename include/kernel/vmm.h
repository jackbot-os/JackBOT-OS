#ifndef JACKBOT_VMM_H
#define JACKBOT_VMM_H

#include <stdint.h>

#define PTE_PRESENT (1ull << 0)
#define PTE_WRITABLE (1ull << 1)
#define PTE_USER (1ull << 2)
#define PTE_PWT (1ull << 3)
#define PTE_PCD (1ull << 4)
#define PTE_HUGE (1ull << 7)
#define PTE_NX (1ull << 63)

#define KERNEL_PTE_FLAGS (PTE_PRESENT | PTE_WRITABLE)

void vmm_init(void);
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_phys_of(uint64_t virt);
void vmm_invlpg(uint64_t virt);
void vmm_map_in(uint64_t p4phys, uint64_t virt, uint64_t phys, uint64_t flags);

#endif
