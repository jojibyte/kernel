#include "pmm.h"
#include "console.h"
#include "multiboot2.h"

static uint64_t *page_bitmap;
static struct Page *page_array;
static uint64_t bitmap_size;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t total_memory;

extern char __kernel_start[];
extern char __kernel_end[];

static inline void bitmap_set(uint64_t page) {
    page_bitmap[page / 64] |= (1ULL << (page % 64));
}

static inline void bitmap_clear(uint64_t page) {
    page_bitmap[page / 64] &= ~(1ULL << (page % 64));
}

static inline bool bitmap_test(uint64_t page) {
    return (page_bitmap[page / 64] & (1ULL << (page % 64))) != 0;
}

static uint64_t bitmap_find_free(void) {
    for (uint64_t i = 0; i < bitmap_size; i++) {
        if (page_bitmap[i] != ~0ULL) {
            for (int j = 0; j < 64; j++) {
                if ((page_bitmap[i] & (1ULL << j)) == 0) {
                    return i * 64 + j;
                }
            }
        }
    }
    return (uint64_t)-1;
}

static uint64_t bitmap_find_free_contiguous(size_t count) {
    uint64_t consecutive = 0;
    uint64_t start = 0;
    
    for (uint64_t page = 0; page < total_pages; page++) {
        if (!bitmap_test(page)) {
            if (consecutive == 0) {
                start = page;
            }
            consecutive++;
            if (consecutive >= count) {
                return start;
            }
        } else {
            consecutive = 0;
        }
    }
    
    return (uint64_t)-1;
}

void pmm_init(void) {
    phys_addr_t max_addr = 0;
    total_memory = 0;
    
    for (uint32_t i = 0; i < boot_info.mmap_entry_count; i++) {
        struct Multiboot2MmapEntry *entry = 
            (void *)((uint8_t *)boot_info.mmap_entries + i * boot_info.mmap_entry_size);
        
        if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            phys_addr_t end = entry->addr + entry->len;
            if (end > max_addr) {
                max_addr = end;
            }
            total_memory += entry->len;
        }
    }
    
    total_pages = max_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 63) / 64;
    
    page_bitmap = (uint64_t *)ALIGN_UP((uint64_t)__kernel_end, PAGE_SIZE);
    page_array = (struct Page *)ALIGN_UP((uint64_t)page_bitmap + bitmap_size * 8, PAGE_SIZE);
    
    for (uint64_t i = 0; i < bitmap_size; i++) {
        page_bitmap[i] = ~0ULL;
    }
    
    for (uint64_t i = 0; i < total_pages; i++) {
        page_array[i].flags = 0;
        page_array[i].ref_count = 0;
        page_array[i].order = 0;
        page_array[i].next = NULL;
    }
    
    free_pages = 0;
    
    for (uint32_t i = 0; i < boot_info.mmap_entry_count; i++) {
        struct Multiboot2MmapEntry *entry = 
            (void *)((uint8_t *)boot_info.mmap_entries + i * boot_info.mmap_entry_size);
        
        if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            phys_addr_t start = ALIGN_UP(entry->addr, PAGE_SIZE);
            phys_addr_t end = ALIGN_DOWN(entry->addr + entry->len, PAGE_SIZE);
            
            for (phys_addr_t addr = start; addr < end; addr += PAGE_SIZE) {
                uint64_t page = addr / PAGE_SIZE;
                if (page < total_pages) {
                    bitmap_clear(page);
                    free_pages++;
                }
            }
        }
    }
    
    phys_addr_t kernel_phys_start = (uint64_t)__kernel_start;
    phys_addr_t kernel_phys_end = (uint64_t)page_array + total_pages * sizeof(struct Page);
    
    pmm_reserve_range(kernel_phys_start, kernel_phys_end);
    
    pmm_reserve_range(0, 0x100000);
}

phys_addr_t pmm_alloc_page(void) {
    uint64_t page = bitmap_find_free();
    
    if (page == (uint64_t)-1) {
        return 0;
    }
    
    bitmap_set(page);
    free_pages--;
    
    page_array[page].ref_count = 1;
    
    return page * PAGE_SIZE;
}

phys_addr_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();
    
    uint64_t start = bitmap_find_free_contiguous(count);
    
    if (start == (uint64_t)-1) {
        return 0;
    }
    
    for (size_t i = 0; i < count; i++) {
        bitmap_set(start + i);
        page_array[start + i].ref_count = 1;
    }
    free_pages -= count;
    
    return start * PAGE_SIZE;
}

void pmm_free_page(phys_addr_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    
    if (page >= total_pages) return;
    if (!bitmap_test(page)) return;
    
    bitmap_clear(page);
    free_pages++;
}

void pmm_free_pages(phys_addr_t addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(addr + i * PAGE_SIZE);
    }
}

void pmm_reserve_range(phys_addr_t start, phys_addr_t end) {
    start = ALIGN_DOWN(start, PAGE_SIZE);
    end = ALIGN_UP(end, PAGE_SIZE);
    
    for (phys_addr_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t page = addr / PAGE_SIZE;
        if (page < total_pages && !bitmap_test(page)) {
            bitmap_set(page);
            free_pages--;
        }
    }
}

uint64_t pmm_get_free_memory(void) {
    return free_pages * PAGE_SIZE;
}

uint64_t pmm_get_total_memory(void) {
    return total_memory;
}

uint64_t pmm_get_used_memory(void) {
    return (total_pages - free_pages) * PAGE_SIZE;
}

struct Page *pmm_get_page(phys_addr_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page >= total_pages) return NULL;
    return &page_array[page];
}
