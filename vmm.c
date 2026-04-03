#include "vmm.h"
#include "pmm.h"
#include "x86_64.h"
#include "console.h"

typedef uint64_t pte_t;

#define RECURSIVE_SLOT  510

static phys_addr_t kernel_pml4;
static struct AddressSpace kernel_as;

static void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

static void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static pte_t *get_pte(virt_addr_t virt, bool create) {
    pte_t *pml4 = (pte_t *)kernel_pml4;
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);
    
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        phys_addr_t pdpt = pmm_alloc_page();
        if (!pdpt) return NULL;
        memset((void *)pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx] = pdpt | PTE_PRESENT | PTE_WRITABLE;
    }
    
    pte_t *pdpt = (pte_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        phys_addr_t pd = pmm_alloc_page();
        if (!pd) return NULL;
        memset((void *)pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx] = pd | PTE_PRESENT | PTE_WRITABLE;
    }
    
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        return NULL;
    }
    
    pte_t *pd = (pte_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        phys_addr_t pt = pmm_alloc_page();
        if (!pt) return NULL;
        memset((void *)pt, 0, PAGE_SIZE);
        pd[pd_idx] = pt | PTE_PRESENT | PTE_WRITABLE;
    }
    
    if (pd[pd_idx] & PTE_HUGE) {
        return NULL;
    }
    
    pte_t *pt = (pte_t *)(pd[pd_idx] & PTE_ADDR_MASK);
    return &pt[pt_idx];
}

void vmm_init(void) {
    kernel_pml4 = read_cr3() & PTE_ADDR_MASK;
    
    kernel_as.pml4 = kernel_pml4;
    kernel_as.start = KERNEL_VIRT_BASE;
    kernel_as.end = 0xFFFFFFFFFFFFFFFFULL;
    kernel_as.page_count = 0;
    
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
    if (edx & BIT(20)) {
        uint64_t efer = rdmsr(MSR_EFER);
        efer |= EFER_NXE;
        wrmsr(MSR_EFER, efer);
    }
    
    write_cr4(read_cr4() | CR4_PGE);
}

void vmm_map_page(virt_addr_t virt, phys_addr_t phys, uint64_t flags) {
    pte_t *pte = get_pte(virt, true);
    if (!pte) {
        return;
    }
    
    *pte = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    invlpg(virt);
}

void vmm_unmap_page(virt_addr_t virt) {
    pte_t *pte = get_pte(virt, false);
    if (pte) {
        *pte = 0;
        invlpg(virt);
    }
}

phys_addr_t vmm_get_phys(virt_addr_t virt) {
    pte_t *pte = get_pte(virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return 0;
    }
    return (*pte & PTE_ADDR_MASK) | PAGE_OFFSET(virt);
}

bool vmm_is_mapped(virt_addr_t virt) {
    pte_t *pte = get_pte(virt, false);
    return pte && (*pte & PTE_PRESENT);
}

struct AddressSpace *vmm_create_address_space(void) {
    struct AddressSpace *as = (struct AddressSpace *)pmm_alloc_page();
    if (!as) return NULL;
    
    as->pml4 = pmm_alloc_page();
    if (!as->pml4) {
        pmm_free_page((phys_addr_t)as);
        return NULL;
    }
    
    pte_t *new_pml4 = (pte_t *)as->pml4;
    pte_t *kern_pml4 = (pte_t *)kernel_pml4;
    
    memset(new_pml4, 0, PAGE_SIZE / 2);
    memcpy(&new_pml4[256], &kern_pml4[256], PAGE_SIZE / 2);
    
    as->start = 0x1000;
    as->end = 0x00007FFFFFFFFFFFULL;
    as->page_count = 0;
    
    return as;
}

void vmm_destroy_address_space(struct AddressSpace *as) {
    if (!as || as == &kernel_as) return;
    
    pmm_free_page(as->pml4);
    pmm_free_page((phys_addr_t)as);
}

void vmm_switch_address_space(struct AddressSpace *as) {
    if (as && as->pml4 != read_cr3()) {
        write_cr3(as->pml4);
    }
}

struct AddressSpace *vmm_get_kernel_address_space(void) {
    return &kernel_as;
}

void vmm_map_kernel_page(virt_addr_t virt, phys_addr_t phys, uint64_t flags) {
    vmm_map_page(virt, phys, flags | PTE_GLOBAL);
}

static virt_addr_t kernel_heap_end = KERNEL_VIRT_BASE + 0x10000000;

virt_addr_t vmm_alloc_kernel_pages(size_t count) {
    virt_addr_t virt = kernel_heap_end;
    
    for (size_t i = 0; i < count; i++) {
        phys_addr_t phys = pmm_alloc_page();
        if (!phys) {
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(virt + j * PAGE_SIZE);
            }
            return 0;
        }
        vmm_map_kernel_page(virt + i * PAGE_SIZE, phys, PTE_WRITABLE);
    }
    
    kernel_heap_end += count * PAGE_SIZE;
    return virt;
}

void vmm_free_kernel_pages(virt_addr_t addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        phys_addr_t phys = vmm_get_phys(addr + i * PAGE_SIZE);
        if (phys) {
            vmm_unmap_page(addr + i * PAGE_SIZE);
            pmm_free_page(phys);
        }
    }
}
