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

static virt_addr_t kernel_heap_end = 0xFFFFFFFFC0000000ULL;

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

static pte_t *get_pte_in_space(struct AddressSpace *as, virt_addr_t virt, bool create) {
    if (!as) return NULL;
    
    pte_t *pml4 = (pte_t *)as->pml4;
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx = PD_INDEX(virt);
    uint64_t pt_idx = PT_INDEX(virt);
    
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        phys_addr_t pdpt = pmm_alloc_page();
        if (!pdpt) return NULL;
        memset((void *)pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx] = pdpt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    pte_t *pdpt = (pte_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        phys_addr_t pd = pmm_alloc_page();
        if (!pd) return NULL;
        memset((void *)pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx] = pd | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    if (pdpt[pdpt_idx] & PTE_HUGE) return NULL;
    
    pte_t *pd = (pte_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        phys_addr_t pt = pmm_alloc_page();
        if (!pt) return NULL;
        memset((void *)pt, 0, PAGE_SIZE);
        pd[pd_idx] = pt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    if (pd[pd_idx] & PTE_HUGE) return NULL;
    
    pte_t *pt = (pte_t *)(pd[pd_idx] & PTE_ADDR_MASK);
    return &pt[pt_idx];
}

struct AddressSpace *vmm_clone_address_space(struct AddressSpace *src) {
    if (!src) return NULL;
    
    struct AddressSpace *dst = vmm_create_address_space();
    if (!dst) return NULL;
    
    pte_t *src_pml4 = (pte_t *)src->pml4;
    pte_t *dst_pml4 = (pte_t *)dst->pml4;
    
    for (int i = 0; i < 256; i++) {
        if (!(src_pml4[i] & PTE_PRESENT)) continue;
        
        pte_t *src_pdpt = (pte_t *)(src_pml4[i] & PTE_ADDR_MASK);
        
        phys_addr_t dst_pdpt_phys = pmm_alloc_page();
        if (!dst_pdpt_phys) {
            vmm_destroy_address_space(dst);
            return NULL;
        }
        
        pte_t *dst_pdpt = (pte_t *)dst_pdpt_phys;
        memset(dst_pdpt, 0, PAGE_SIZE);
        dst_pml4[i] = dst_pdpt_phys | (src_pml4[i] & ~PTE_ADDR_MASK);
        
        for (int j = 0; j < 512; j++) {
            if (!(src_pdpt[j] & PTE_PRESENT)) continue;
            if (src_pdpt[j] & PTE_HUGE) continue;
            
            pte_t *src_pd = (pte_t *)(src_pdpt[j] & PTE_ADDR_MASK);
            
            phys_addr_t dst_pd_phys = pmm_alloc_page();
            if (!dst_pd_phys) {
                vmm_destroy_address_space(dst);
                return NULL;
            }
            
            pte_t *dst_pd = (pte_t *)dst_pd_phys;
            memset(dst_pd, 0, PAGE_SIZE);
            dst_pdpt[j] = dst_pd_phys | (src_pdpt[j] & ~PTE_ADDR_MASK);
            
            for (int k = 0; k < 512; k++) {
                if (!(src_pd[k] & PTE_PRESENT)) continue;
                if (src_pd[k] & PTE_HUGE) continue;
                
                pte_t *src_pt = (pte_t *)(src_pd[k] & PTE_ADDR_MASK);
                
                phys_addr_t dst_pt_phys = pmm_alloc_page();
                if (!dst_pt_phys) {
                    vmm_destroy_address_space(dst);
                    return NULL;
                }
                
                pte_t *dst_pt = (pte_t *)dst_pt_phys;
                memset(dst_pt, 0, PAGE_SIZE);
                dst_pd[k] = dst_pt_phys | (src_pd[k] & ~PTE_ADDR_MASK);
                
                for (int l = 0; l < 512; l++) {
                    if (!(src_pt[l] & PTE_PRESENT)) continue;
                    
                    phys_addr_t page_phys = src_pt[l] & PTE_ADDR_MASK;
                    uint64_t flags = src_pt[l] & ~PTE_ADDR_MASK;
                    
                    if (flags & PTE_WRITABLE) {
                        flags &= ~PTE_WRITABLE;
                        flags |= PTE_COW;
                        src_pt[l] = page_phys | flags;
                    }
                    
                    dst_pt[l] = page_phys | flags;
                    
                    struct Page *page = pmm_get_page(page_phys);
                    if (page) {
                        page->ref_count++;
                    }
                }
            }
        }
    }
    
    dst->start = src->start;
    dst->end = src->end;
    dst->page_count = src->page_count;
    
    return dst;
}

int vmm_copy_page_range(struct AddressSpace *dst, struct AddressSpace *src,
                        virt_addr_t start, virt_addr_t end) {
    if (!dst || !src) return -EINVAL;
    
    for (virt_addr_t addr = start; addr < end; addr += PAGE_SIZE) {
        pte_t *src_pte = get_pte_in_space(src, addr, false);
        if (!src_pte || !(*src_pte & PTE_PRESENT)) continue;
        
        pte_t *dst_pte = get_pte_in_space(dst, addr, true);
        if (!dst_pte) return -ENOMEM;
        
        phys_addr_t page_phys = *src_pte & PTE_ADDR_MASK;
        uint64_t flags = *src_pte & ~PTE_ADDR_MASK;
        
        if (flags & PTE_WRITABLE) {
            flags &= ~PTE_WRITABLE;
            flags |= PTE_COW;
            *src_pte = page_phys | flags;
            invlpg(addr);
        }
        
        *dst_pte = page_phys | flags;
        
        struct Page *page = pmm_get_page(page_phys);
        if (page) {
            page->ref_count++;
        }
    }
    
    return 0;
}

void vmm_mark_cow(struct AddressSpace *as, virt_addr_t start, virt_addr_t end) {
    if (!as) return;
    
    for (virt_addr_t addr = start; addr < end; addr += PAGE_SIZE) {
        pte_t *pte = get_pte_in_space(as, addr, false);
        if (!pte || !(*pte & PTE_PRESENT)) continue;
        
        if (*pte & PTE_WRITABLE) {
            *pte &= ~PTE_WRITABLE;
            *pte |= PTE_COW;
            invlpg(addr);
        }
    }
}

int vmm_handle_cow_fault(virt_addr_t fault_addr) {
    virt_addr_t page_addr = ALIGN_DOWN(fault_addr, PAGE_SIZE);
    
    pte_t *pte = get_pte(page_addr, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return -EFAULT;
    }
    
    if (!(*pte & PTE_COW)) {
        return -EACCES;
    }
    
    phys_addr_t old_phys = *pte & PTE_ADDR_MASK;
    struct Page *old_page = pmm_get_page(old_phys);
    
    if (old_page && old_page->ref_count == 1) {
        *pte &= ~PTE_COW;
        *pte |= PTE_WRITABLE;
        invlpg(page_addr);
        return 0;
    }
    
    phys_addr_t new_phys = pmm_alloc_page();
    if (!new_phys) {
        return -ENOMEM;
    }
    
    memcpy((void *)new_phys, (void *)old_phys, PAGE_SIZE);
    
    uint64_t flags = *pte & ~PTE_ADDR_MASK;
    flags &= ~PTE_COW;
    flags |= PTE_WRITABLE;
    
    *pte = new_phys | flags;
    invlpg(page_addr);
    
    if (old_page) {
        old_page->ref_count--;
        if (old_page->ref_count == 0) {
            pmm_free_page(old_phys);
        }
    }
    
    return 0;
}
