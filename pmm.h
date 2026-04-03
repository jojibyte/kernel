#ifndef _PMM_H
#define _PMM_H

#include "types.h"

#define ZONE_DMA        0
#define ZONE_NORMAL     1
#define ZONE_HIGH       2
#define ZONE_COUNT      3

#define PAGE_FREE       0
#define PAGE_USED       1
#define PAGE_RESERVED   2
#define PAGE_KERNEL     3

struct Page {
    uint32_t flags;
    uint32_t ref_count;
    uint32_t order;
    struct Page *next;
};

void pmm_init(void);

phys_addr_t pmm_alloc_page(void);
phys_addr_t pmm_alloc_pages(size_t count);
void pmm_free_page(phys_addr_t addr);
void pmm_free_pages(phys_addr_t addr, size_t count);

struct Page *pmm_get_page(phys_addr_t addr);
void pmm_reserve_range(phys_addr_t start, phys_addr_t end);

uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_used_memory(void);

#endif
