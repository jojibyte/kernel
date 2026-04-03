#ifndef _VMM_H
#define _VMM_H

#include "types.h"

#define PTE_PRESENT     BIT(0)
#define PTE_WRITABLE    BIT(1)
#define PTE_USER        BIT(2)
#define PTE_WRITETHROUGH BIT(3)
#define PTE_NOCACHE     BIT(4)
#define PTE_ACCESSED    BIT(5)
#define PTE_DIRTY       BIT(6)
#define PTE_HUGE        BIT(7)
#define PTE_GLOBAL      BIT(8)
#define PTE_NX          BIT(63)

#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

#define KERNEL_VIRT_BASE    0xFFFFFFFF80000000ULL
#define KERNEL_PHYS_BASE    0x0ULL

#define PML4_INDEX(addr)    (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> 12) & 0x1FF)
#define PAGE_OFFSET(addr)   ((addr) & 0xFFF)

struct AddressSpace {
    phys_addr_t pml4;
    virt_addr_t start;
    virt_addr_t end;
    size_t page_count;
};

struct VmRegion {
    virt_addr_t start;
    virt_addr_t end;
    uint64_t flags;
    struct VmRegion *next;
};

void vmm_init(void);

void vmm_map_page(virt_addr_t virt, phys_addr_t phys, uint64_t flags);
void vmm_unmap_page(virt_addr_t virt);
phys_addr_t vmm_get_phys(virt_addr_t virt);
bool vmm_is_mapped(virt_addr_t virt);

struct AddressSpace *vmm_create_address_space(void);
void vmm_destroy_address_space(struct AddressSpace *as);
void vmm_switch_address_space(struct AddressSpace *as);
struct AddressSpace *vmm_get_kernel_address_space(void);

void vmm_map_kernel_page(virt_addr_t virt, phys_addr_t phys, uint64_t flags);
virt_addr_t vmm_alloc_kernel_pages(size_t count);
void vmm_free_kernel_pages(virt_addr_t addr, size_t count);

static inline virt_addr_t phys_to_virt(phys_addr_t phys) {
    return phys + KERNEL_VIRT_BASE;
}

static inline phys_addr_t virt_to_phys(virt_addr_t virt) {
    if (virt >= KERNEL_VIRT_BASE)
        return virt - KERNEL_VIRT_BASE;
    return vmm_get_phys(virt);
}

#endif
