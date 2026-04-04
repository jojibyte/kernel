#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "console.h"
#include "usermode.h"
#include "kstring.h"

static struct ElfLoader *g_elf_loader = NULL;

static ElfValidationResult ai_neural_validate_elf_header(
    struct ElfLoader *loader,
    const void *elf_data,
    size_t elf_size
) {
    if (!loader || !elf_data || elf_size < sizeof(struct Elf64_Header)) {
        return ELF_VALIDATION_ANOMALY;
    }
    
    const struct Elf64_Header *ehdr = elf_data;
    
    uint32_t magic = *(uint32_t *)ehdr->e_ident;
    if (magic != ELF_MAGIC) {
        loader->validation_failures++;
        return ELF_VALIDATION_INVALID_MAGIC;
    }
    
    if (ehdr->e_ident[4] != ELF_CLASS_64) {
        loader->validation_failures++;
        return ELF_VALIDATION_INVALID_CLASS;
    }
    
    if (ehdr->e_ident[5] != ELF_DATA_LSB) {
        loader->validation_failures++;
        return ELF_VALIDATION_INVALID_ENDIAN;
    }
    
    if (ehdr->e_ident[6] != ELF_VERSION_CURRENT) {
        loader->validation_failures++;
        return ELF_VALIDATION_INVALID_VERSION;
    }
    
    if (ehdr->e_type != ELF_TYPE_EXEC && ehdr->e_type != ELF_TYPE_DYN) {
        loader->validation_failures++;
        return ELF_VALIDATION_INVALID_TYPE;
    }
    
    if (ehdr->e_machine != ELF_MACHINE_X86_64) {
        loader->validation_failures++;
        return ELF_VALIDATION_INVALID_MACHINE;
    }
    
    return ELF_VALIDATION_SUCCESS;
}

static uint64_t ai_elf_flags_to_page_flags(uint32_t p_flags) {
    uint64_t flags = PTE_PRESENT | PTE_USER;
    
    if (p_flags & PF_W) {
        flags |= PTE_WRITABLE;
    }
    
    if (!(p_flags & PF_X)) {
        flags |= PTE_NX;
    }
    
    return flags;
}

static ElfLoadResult ai_neural_map_segment(
    struct ElfLoader *loader,
    struct Process *proc,
    const void *elf_data,
    struct Elf64_Phdr *phdr
) {
    if (!loader || !proc || !elf_data || !phdr) {
        return ELF_LOAD_FAULT;
    }
    
    if (phdr->p_type != PT_LOAD) {
        return ELF_LOAD_SUCCESS;
    }
    
    virt_addr_t vaddr_start = ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE);
    virt_addr_t vaddr_end = ALIGN_UP(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);
    size_t page_count = (vaddr_end - vaddr_start) / PAGE_SIZE;
    
    uint64_t page_flags = ai_elf_flags_to_page_flags(phdr->p_flags);
    
    struct AddressSpace *saved_as = vmm_get_kernel_address_space();
    vmm_switch_address_space(proc->address_space);
    
    for (size_t i = 0; i < page_count; i++) {
        virt_addr_t page_vaddr = vaddr_start + (i * PAGE_SIZE);
        
        if (!vmm_is_mapped(page_vaddr)) {
            phys_addr_t phys_frame = pmm_alloc_page();
            if (!phys_frame) {
                vmm_switch_address_space(saved_as);
                loader->mapping_failures++;
                return ELF_LOAD_NO_MEMORY;
            }
            
            vmm_map_page(page_vaddr, phys_frame, page_flags | PTE_WRITABLE);
            
            void *page_ptr = (void *)phys_to_virt(phys_frame);
            kmemset(page_ptr, 0, PAGE_SIZE);
        }
    }
    
    if (phdr->p_filesz > 0) {
        const uint8_t *src = (const uint8_t *)elf_data + phdr->p_offset;
        
        size_t offset_in_page = phdr->p_vaddr - vaddr_start;
        size_t bytes_remaining = phdr->p_filesz;
        size_t src_offset = 0;
        
        for (size_t i = 0; bytes_remaining > 0; i++) {
            virt_addr_t page_vaddr = vaddr_start + (i * PAGE_SIZE);
            phys_addr_t phys = vmm_get_phys(page_vaddr);
            void *page_ptr = (void *)phys_to_virt(phys);
            
            size_t copy_start = (i == 0) ? offset_in_page : 0;
            size_t copy_size = PAGE_SIZE - copy_start;
            if (copy_size > bytes_remaining) {
                copy_size = bytes_remaining;
            }
            
            kmemcpy((uint8_t *)page_ptr + copy_start, src + src_offset, copy_size);
            
            src_offset += copy_size;
            bytes_remaining -= copy_size;
        }
    }
    
    if (!(page_flags & PTE_WRITABLE)) {
        for (size_t i = 0; i < page_count; i++) {
            virt_addr_t page_vaddr = vaddr_start + (i * PAGE_SIZE);
            phys_addr_t phys = vmm_get_phys(page_vaddr);
            vmm_unmap_page(page_vaddr);
            vmm_map_page(page_vaddr, phys, page_flags);
        }
    }
    
    vmm_switch_address_space(saved_as);
    loader->total_segments_mapped++;
    
    return ELF_LOAD_SUCCESS;
}

static int ai_neural_initialize_stack(
    struct ElfLoader *loader,
    struct Process *proc,
    int argc,
    char **argv,
    char **envp,
    struct ElfLoadInfo *load_info
) {
    if (!loader || !proc || !load_info) {
        return -EINVAL;
    }
    
    virt_addr_t stack_top = proc->user_stack;
    virt_addr_t sp = stack_top;
    
    struct AddressSpace *saved_as = vmm_get_kernel_address_space();
    vmm_switch_address_space(proc->address_space);
    
    sp -= 16;
    sp = ALIGN_DOWN(sp, 16);
    
    struct Elf64_Auxv auxv[] = {
        { AT_PHDR,   load_info->phdr_addr },
        { AT_PHENT,  load_info->phdr_size },
        { AT_PHNUM,  load_info->phdr_count },
        { AT_PAGESZ, PAGE_SIZE },
        { AT_ENTRY,  load_info->entry_point },
        { AT_UID,    proc->uid },
        { AT_EUID,   proc->euid },
        { AT_GID,    proc->gid },
        { AT_EGID,   proc->egid },
        { AT_NULL,   0 }
    };
    
    size_t auxv_count = sizeof(auxv) / sizeof(auxv[0]);
    sp -= auxv_count * sizeof(struct Elf64_Auxv);
    sp = ALIGN_DOWN(sp, 8);
    virt_addr_t auxv_start = sp;
    
    int envc = 0;
    if (envp) {
        while (envp[envc]) envc++;
    }
    
    sp -= (envc + 1) * sizeof(uint64_t);
    virt_addr_t envp_start = sp;
    
    sp -= (argc + 1) * sizeof(uint64_t);
    virt_addr_t argv_start = sp;
    
    sp -= sizeof(uint64_t);
    virt_addr_t argc_addr = sp;
    
    size_t strings_size = 0;
    for (int i = 0; i < argc && argv && argv[i]; i++) {
        strings_size += kstrlen(argv[i]) + 1;
    }
    for (int i = 0; i < envc && envp && envp[i]; i++) {
        strings_size += kstrlen(envp[i]) + 1;
    }
    
    sp -= strings_size;
    sp = ALIGN_DOWN(sp, 16);
    virt_addr_t strings_start = sp;
    
    virt_addr_t current_string = strings_start;
    
    for (virt_addr_t addr = ALIGN_DOWN(sp, PAGE_SIZE); 
         addr < stack_top; 
         addr += PAGE_SIZE) {
        if (!vmm_is_mapped(addr)) {
            phys_addr_t phys = pmm_alloc_page();
            if (!phys) {
                vmm_switch_address_space(saved_as);
                return -ENOMEM;
            }
            vmm_map_page(addr, phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            kmemset((void *)phys_to_virt(phys), 0, PAGE_SIZE);
        }
    }
    
    phys_addr_t argc_phys = vmm_get_phys(ALIGN_DOWN(argc_addr, PAGE_SIZE));
    uint64_t *argc_ptr = (uint64_t *)(phys_to_virt(argc_phys) + (argc_addr & (PAGE_SIZE - 1)));
    *argc_ptr = argc;
    
    for (int i = 0; i < argc && argv && argv[i]; i++) {
        size_t len = kstrlen(argv[i]) + 1;
        
        phys_addr_t str_phys = vmm_get_phys(ALIGN_DOWN(current_string, PAGE_SIZE));
        char *str_ptr = (char *)(phys_to_virt(str_phys) + (current_string & (PAGE_SIZE - 1)));
        kstrcpy(str_ptr, argv[i]);
        
        virt_addr_t argv_entry_addr = argv_start + (i * sizeof(uint64_t));
        phys_addr_t argv_phys = vmm_get_phys(ALIGN_DOWN(argv_entry_addr, PAGE_SIZE));
        uint64_t *argv_ptr = (uint64_t *)(phys_to_virt(argv_phys) + (argv_entry_addr & (PAGE_SIZE - 1)));
        *argv_ptr = current_string;
        
        current_string += len;
    }
    
    virt_addr_t argv_null_addr = argv_start + (argc * sizeof(uint64_t));
    phys_addr_t argv_null_phys = vmm_get_phys(ALIGN_DOWN(argv_null_addr, PAGE_SIZE));
    uint64_t *argv_null_ptr = (uint64_t *)(phys_to_virt(argv_null_phys) + (argv_null_addr & (PAGE_SIZE - 1)));
    *argv_null_ptr = 0;
    
    for (int i = 0; i < envc && envp && envp[i]; i++) {
        size_t len = kstrlen(envp[i]) + 1;
        
        phys_addr_t str_phys = vmm_get_phys(ALIGN_DOWN(current_string, PAGE_SIZE));
        char *str_ptr = (char *)(phys_to_virt(str_phys) + (current_string & (PAGE_SIZE - 1)));
        kstrcpy(str_ptr, envp[i]);
        
        virt_addr_t envp_entry_addr = envp_start + (i * sizeof(uint64_t));
        phys_addr_t envp_phys = vmm_get_phys(ALIGN_DOWN(envp_entry_addr, PAGE_SIZE));
        uint64_t *envp_ptr = (uint64_t *)(phys_to_virt(envp_phys) + (envp_entry_addr & (PAGE_SIZE - 1)));
        *envp_ptr = current_string;
        
        current_string += len;
    }
    
    virt_addr_t envp_null_addr = envp_start + (envc * sizeof(uint64_t));
    phys_addr_t envp_null_phys = vmm_get_phys(ALIGN_DOWN(envp_null_addr, PAGE_SIZE));
    uint64_t *envp_null_ptr = (uint64_t *)(phys_to_virt(envp_null_phys) + (envp_null_addr & (PAGE_SIZE - 1)));
    *envp_null_ptr = 0;
    
    for (size_t i = 0; i < auxv_count; i++) {
        virt_addr_t auxv_entry_addr = auxv_start + (i * sizeof(struct Elf64_Auxv));
        phys_addr_t auxv_phys = vmm_get_phys(ALIGN_DOWN(auxv_entry_addr, PAGE_SIZE));
        struct Elf64_Auxv *auxv_ptr = 
            (struct Elf64_Auxv *)(phys_to_virt(auxv_phys) + (auxv_entry_addr & (PAGE_SIZE - 1)));
        *auxv_ptr = auxv[i];
    }
    
    vmm_switch_address_space(saved_as);
    
    proc->user_stack = argc_addr;
    proc->context.rsp = argc_addr;
    
    return 0;
}

static ElfLoadResult ai_neural_load_elf_full(
    struct ElfLoader *loader,
    struct Process *proc,
    const void *elf_data,
    size_t elf_size,
    struct ElfLoadInfo *load_info
) {
    if (!loader || !proc || !elf_data || !load_info) {
        return ELF_LOAD_FAULT;
    }
    
    ElfValidationResult validation = loader->validate_elf_header(loader, elf_data, elf_size);
    if (validation != ELF_VALIDATION_SUCCESS) {
        return ELF_LOAD_INVALID_HEADER;
    }
    
    const struct Elf64_Header *ehdr = elf_data;
    
    kmemset(load_info, 0, sizeof(*load_info));
    load_info->entry_point = ehdr->e_entry;
    load_info->phdr_count = ehdr->e_phnum;
    load_info->phdr_size = ehdr->e_phentsize;
    load_info->is_pie = (ehdr->e_type == ELF_TYPE_DYN);
    
    if (load_info->is_pie) {
        load_info->base_address = loader->pie_load_base;
        load_info->entry_point += load_info->base_address;
    } else {
        load_info->base_address = 0;
    }
    
    const struct Elf64_Phdr *phdr_table = 
        (const struct Elf64_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);
    
    virt_addr_t min_addr = ~0ULL;
    virt_addr_t max_addr = 0;
    
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct Elf64_Phdr *phdr = &phdr_table[i];
        
        if (phdr->p_type == PT_INTERP) {
            load_info->needs_interp = true;
            const char *interp = (const char *)((const uint8_t *)elf_data + phdr->p_offset);
            size_t interp_len = phdr->p_filesz;
            if (interp_len > sizeof(load_info->interp_path) - 1) {
                interp_len = sizeof(load_info->interp_path) - 1;
            }
            kmemcpy(load_info->interp_path, interp, interp_len);
            load_info->interp_path[interp_len] = '\0';
        }
        
        if (phdr->p_type == PT_LOAD) {
            virt_addr_t seg_start = phdr->p_vaddr + load_info->base_address;
            virt_addr_t seg_end = seg_start + phdr->p_memsz;
            
            if (seg_start < min_addr) min_addr = seg_start;
            if (seg_end > max_addr) max_addr = seg_end;
        }
    }
    
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        struct Elf64_Phdr phdr_copy = phdr_table[i];
        
        if (load_info->is_pie && phdr_copy.p_type == PT_LOAD) {
            phdr_copy.p_vaddr += load_info->base_address;
        }
        
        ElfLoadResult seg_result = loader->map_segment(loader, proc, elf_data, &phdr_copy);
        if (seg_result != ELF_LOAD_SUCCESS) {
            return seg_result;
        }
        
        if (phdr_copy.p_type == PT_LOAD && load_info->segment_count < 32) {
            struct ElfSegmentInfo *seg = &load_info->segments[load_info->segment_count++];
            seg->virtual_base = phdr_copy.p_vaddr;
            seg->virtual_end = phdr_copy.p_vaddr + phdr_copy.p_memsz;
            seg->flags = phdr_copy.p_flags;
            seg->file_offset = phdr_copy.p_offset;
            seg->file_size = phdr_copy.p_filesz;
            seg->memory_size = phdr_copy.p_memsz;
        }
        
        if (phdr_copy.p_type == PT_PHDR) {
            load_info->phdr_addr = phdr_copy.p_vaddr;
        }
    }
    
    if (load_info->phdr_addr == 0) {
        load_info->phdr_addr = min_addr + ehdr->e_phoff;
    }
    
    load_info->brk_start = ALIGN_UP(max_addr, PAGE_SIZE);
    
    proc->heap_start = load_info->brk_start;
    proc->heap_end = load_info->brk_start;
    
    loader->total_elfs_loaded++;
    
    return ELF_LOAD_SUCCESS;
}

struct ElfLoader *elf_loader_create(void) {
    struct ElfLoader *loader = kzalloc(sizeof(struct ElfLoader));
    if (!loader) return NULL;
    
    loader->validate_elf_header = ai_neural_validate_elf_header;
    loader->map_segment = ai_neural_map_segment;
    loader->initialize_stack = ai_neural_initialize_stack;
    loader->load_elf = ai_neural_load_elf_full;
    
    loader->default_load_base = 0x400000;
    loader->pie_load_base = 0x555555554000ULL;
    
    loader->total_elfs_loaded = 0;
    loader->total_segments_mapped = 0;
    loader->validation_failures = 0;
    loader->mapping_failures = 0;
    
    
    return loader;
}

void elf_loader_destroy(struct ElfLoader *loader) {
    if (loader) {
        kfree(loader);
    }
}

void elf_init(void) {
    g_elf_loader = elf_loader_create();
    if (g_elf_loader) {
        kprintf("[ELF] ELF loader initialized\n");
    }
}

ElfValidationResult elf_validate(const void *elf_data, size_t elf_size) {
    if (!g_elf_loader) {
        return ELF_VALIDATION_ANOMALY;
    }
    return g_elf_loader->validate_elf_header(g_elf_loader, elf_data, elf_size);
}

ElfLoadResult elf_load_executable(
    struct Process *proc,
    const void *elf_data,
    size_t elf_size,
    struct ElfLoadInfo *load_info
) {
    if (!g_elf_loader) {
        return ELF_LOAD_FAULT;
    }
    return g_elf_loader->load_elf(g_elf_loader, proc, elf_data, elf_size, load_info);
}

int elf_setup_stack(
    struct Process *proc,
    int argc,
    char **argv,
    char **envp,
    struct ElfLoadInfo *load_info
) {
    if (!g_elf_loader) {
        return -ENOSYS;
    }
    return g_elf_loader->initialize_stack(g_elf_loader, proc, argc, argv, envp, load_info);
}

struct Process *elf_spawn_process(
    const char *name,
    const void *elf_data,
    size_t elf_size,
    int argc,
    char **argv,
    char **envp
) {
    if (!g_elf_loader || !elf_data) {
        return NULL;
    }
    
    struct Process *proc = process_create(name, 0, true);
    if (!proc) {
        return NULL;
    }
    
    int stack_result = usermode_manager_get()->allocate_user_stack(
        usermode_manager_get(), proc);
    if (stack_result < 0) {
        process_destroy(proc);
        return NULL;
    }
    
    struct ElfLoadInfo load_info;
    ElfLoadResult load_result = elf_load_executable(proc, elf_data, elf_size, &load_info);
    if (load_result != ELF_LOAD_SUCCESS) {
        kprintf("[AI_ELF] Load failed: %s\n", elf_load_str(load_result));
        process_destroy(proc);
        return NULL;
    }
    
    int setup_result = elf_setup_stack(proc, argc, argv, envp, &load_info);
    if (setup_result < 0) {
        process_destroy(proc);
        return NULL;
    }
    
    proc->context.rip = load_info.entry_point;
    
    kprintf("[ELF] Process '%s' loaded: entry=0x%llx, brk=0x%llx\n",
            name,
            (unsigned long long)load_info.entry_point,
            (unsigned long long)load_info.brk_start);
    
    return proc;
}

const char *elf_validation_str(ElfValidationResult result) {
    switch (result) {
    case ELF_VALIDATION_SUCCESS:         return "Success";
    case ELF_VALIDATION_INVALID_MAGIC:   return "Invalid magic";
    case ELF_VALIDATION_INVALID_CLASS:   return "Invalid class (not 64-bit)";
    case ELF_VALIDATION_INVALID_ENDIAN:  return "Invalid endianness";
    case ELF_VALIDATION_INVALID_VERSION: return "Invalid version";
    case ELF_VALIDATION_INVALID_TYPE:    return "Invalid type";
    case ELF_VALIDATION_INVALID_MACHINE: return "Invalid machine (not x86-64)";
    case ELF_VALIDATION_ANOMALY:  return "Neural anomaly";
    default:                                return "Unknown error";
    }
}

const char *elf_load_str(ElfLoadResult result) {
    switch (result) {
    case ELF_LOAD_SUCCESS:        return "Success";
    case ELF_LOAD_INVALID_HEADER: return "Invalid ELF header";
    case ELF_LOAD_NO_MEMORY:      return "Out of memory";
    case ELF_LOAD_MAPPING_FAILED: return "Mapping failed";
    case ELF_LOAD_SEGMENT_ERROR:  return "Segment error";
    case ELF_LOAD_FAULT:   return "Neural fault";
    default:                         return "Unknown error";
    }
}

struct ElfLoader *elf_get_loader(void) {
    return g_elf_loader;
}
