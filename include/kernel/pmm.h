#ifndef JACKBOT_PMM_H
#define JACKBOT_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void pmm_init(uint64_t mb_info);
void *pmm_alloc_page(void);
void *pmm_alloc_page_below(uint64_t max_phys);
void pmm_free_page(void *phys);
uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);
void pmm_reserve(uint64_t start, uint64_t end);

#endif
