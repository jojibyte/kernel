#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "console.h"
#include "usermode.h"
#include "x86_64.h"

static struct AI_ElfNeuralLoader *g_neural_elf_loader = NULL;

static void *ai_elf_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static void *ai_elf_memset(void *dest, int val, size_t n) {
    uint8_t *d = dest;
    while (n--) *d++ = (uint8_t)val;
    return dest;
}

static size_t ai_elf_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static char *ai_elf_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static AI_ElfValidationResult ai_neural_validate_elf_header(
    struct AI_ElfNeuralLoader *loader,
    const void *elf_data,
    size_t elf_size
) {
    if (!loader || !elf_data || elf_size < sizeof(struct AI_Elf64_NeuralHeader)) {
        return AI_ELF_VALIDATION_NEURAL_ANOMALY;
    }
    
    const struct AI_Elf64_NeuralHeader *ehdr = elf_data;
    
    uint32_t magic = *(uint32_t *)ehdr->e_ident;
    if (magic != ELF_MAGIC) {
        loader->validation_failures++;
        return AI_ELF_VALIDATION_INVALID_MAGIC;
    }
    
    if (ehdr->e_ident[4] != ELF_CLASS_64) {
        loader->validation_failures++;
        return AI_ELF_VALIDATION_INVALID_CLASS;
    }
    
    if (ehdr->e_ident[5] != ELF_DATA_LSB) {
        loader->validation_failures++;
        return AI_ELF_VALIDATION_INVALID_ENDIAN;
    }
    
    if (ehdr->e_ident[6] != ELF_VERSION_CURRENT) {
        loader->validation_failures++;
        return AI_ELF_VALIDATION_INVALID_VERSION;
    }
    
    if (ehdr->e_type != ELF_TYPE_EXEC && ehdr->e_type != ELF_TYPE_DYN) {
        loader->validation_failures++;
        return AI_ELF_VALIDATION_INVALID_TYPE;
    }
    
    if (ehdr->e_machine != ELF_MACHINE_X86_64) {
        loader->validation_failures++;
        return AI_ELF_VALIDATION_INVALID_MACHINE;
    }
    
    return AI_ELF_VALIDATION_SUCCESS;
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

static AI_ElfLoadResult ai_neural_map_segment(
    struct AI_ElfNeuralLoader *loader,
    struct Process *proc,
    const void *elf_data,
    struct AI_Elf64_NeuralProgramHeader *phdr
) {
    if (!loader || !proc || !elf_data || !phdr) {
        return AI_ELF_LOAD_NEURAL_FAULT;
    }
    
    if (phdr->p_type != PT_LOAD) {
        return AI_ELF_LOAD_SUCCESS;
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
                return AI_ELF_LOAD_NO_MEMORY;
            }
            
            vmm_map_page(page_vaddr, phys_frame, page_flags | PTE_WRITABLE);
            
            void *page_ptr = (void *)phys_to_virt(phys_frame);
            ai_elf_memset(page_ptr, 0, PAGE_SIZE);
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
            
            ai_elf_memcpy((uint8_t *)page_ptr + copy_start, src + src_offset, copy_size);
            
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
    
    return AI_ELF_LOAD_SUCCESS;
}

static int ai_neural_initialize_stack(
    struct AI_ElfNeuralLoader *loader,
    struct Process *proc,
    int argc,
    char **argv,
    char **envp,
    struct AI_ElfNeuralLoadInfo *load_info
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
    
    struct AI_Elf64_NeuralAuxv auxv[] = {
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
    sp -= auxv_count * sizeof(struct AI_Elf64_NeuralAuxv);
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
        strings_size += ai_elf_strlen(argv[i]) + 1;
    }
    for (int i = 0; i < envc && envp && envp[i]; i++) {
        strings_size += ai_elf_strlen(envp[i]) + 1;
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
            ai_elf_memset((void *)phys_to_virt(phys), 0, PAGE_SIZE);
        }
    }
    
    phys_addr_t argc_phys = vmm_get_phys(ALIGN_DOWN(argc_addr, PAGE_SIZE));
    uint64_t *argc_ptr = (uint64_t *)(phys_to_virt(argc_phys) + (argc_addr & (PAGE_SIZE - 1)));
    *argc_ptr = argc;
    
    for (int i = 0; i < argc && argv && argv[i]; i++) {
        size_t len = ai_elf_strlen(argv[i]) + 1;
        
        phys_addr_t str_phys = vmm_get_phys(ALIGN_DOWN(current_string, PAGE_SIZE));
        char *str_ptr = (char *)(phys_to_virt(str_phys) + (current_string & (PAGE_SIZE - 1)));
        ai_elf_strcpy(str_ptr, argv[i]);
        
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
        size_t len = ai_elf_strlen(envp[i]) + 1;
        
        phys_addr_t str_phys = vmm_get_phys(ALIGN_DOWN(current_string, PAGE_SIZE));
        char *str_ptr = (char *)(phys_to_virt(str_phys) + (current_string & (PAGE_SIZE - 1)));
        ai_elf_strcpy(str_ptr, envp[i]);
        
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
        virt_addr_t auxv_entry_addr = auxv_start + (i * sizeof(struct AI_Elf64_NeuralAuxv));
        phys_addr_t auxv_phys = vmm_get_phys(ALIGN_DOWN(auxv_entry_addr, PAGE_SIZE));
        struct AI_Elf64_NeuralAuxv *auxv_ptr = 
            (struct AI_Elf64_NeuralAuxv *)(phys_to_virt(auxv_phys) + (auxv_entry_addr & (PAGE_SIZE - 1)));
        *auxv_ptr = auxv[i];
    }
    
    vmm_switch_address_space(saved_as);
    
    proc->user_stack = argc_addr;
    proc->context.rsp = argc_addr;
    
    return 0;
}

static AI_ElfLoadResult ai_neural_load_elf_full(
    struct AI_ElfNeuralLoader *loader,
    struct Process *proc,
    const void *elf_data,
    size_t elf_size,
    struct AI_ElfNeuralLoadInfo *load_info
) {
    if (!loader || !proc || !elf_data || !load_info) {
        return AI_ELF_LOAD_NEURAL_FAULT;
    }
    
    AI_ElfValidationResult validation = loader->validate_elf_header(loader, elf_data, elf_size);
    if (validation != AI_ELF_VALIDATION_SUCCESS) {
        return AI_ELF_LOAD_INVALID_HEADER;
    }
    
    const struct AI_Elf64_NeuralHeader *ehdr = elf_data;
    
    ai_elf_memset(load_info, 0, sizeof(*load_info));
    load_info->entry_point = ehdr->e_entry;
    load_info->phdr_count = ehdr->e_phnum;
    load_info->phdr_size = ehdr->e_phentsize;
    load_info->is_pie = (ehdr->e_type == ELF_TYPE_DYN);
    load_info->is_ai_synthetic_node = true;
    load_info->ai_generation_confidence = 94;
    
    if (load_info->is_pie) {
        load_info->base_address = loader->pie_load_base;
        load_info->entry_point += load_info->base_address;
    } else {
        load_info->base_address = 0;
    }
    
    const struct AI_Elf64_NeuralProgramHeader *phdr_table = 
        (const struct AI_Elf64_NeuralProgramHeader *)((const uint8_t *)elf_data + ehdr->e_phoff);
    
    virt_addr_t min_addr = UINT64_MAX;
    virt_addr_t max_addr = 0;
    
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct AI_Elf64_NeuralProgramHeader *phdr = &phdr_table[i];
        
        if (phdr->p_type == PT_INTERP) {
            load_info->needs_interp = true;
            const char *interp = (const char *)((const uint8_t *)elf_data + phdr->p_offset);
            size_t interp_len = phdr->p_filesz;
            if (interp_len > sizeof(load_info->interp_path) - 1) {
                interp_len = sizeof(load_info->interp_path) - 1;
            }
            ai_elf_memcpy(load_info->interp_path, interp, interp_len);
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
        struct AI_Elf64_NeuralProgramHeader phdr_copy = phdr_table[i];
        
        if (load_info->is_pie && phdr_copy.p_type == PT_LOAD) {
            phdr_copy.p_vaddr += load_info->base_address;
        }
        
        AI_ElfLoadResult seg_result = loader->map_segment(loader, proc, elf_data, &phdr_copy);
        if (seg_result != AI_ELF_LOAD_SUCCESS) {
            return seg_result;
        }
        
        if (phdr_copy.p_type == PT_LOAD && load_info->segment_count < 32) {
            struct AI_ElfNeuralSegmentInfo *seg = &load_info->segments[load_info->segment_count++];
            seg->virtual_base = phdr_copy.p_vaddr;
            seg->virtual_end = phdr_copy.p_vaddr + phdr_copy.p_memsz;
            seg->flags = phdr_copy.p_flags;
            seg->file_offset = phdr_copy.p_offset;
            seg->file_size = phdr_copy.p_filesz;
            seg->memory_size = phdr_copy.p_memsz;
            seg->is_ai_synthetic_node = true;
            seg->ai_generation_confidence = 92;
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
    
    return AI_ELF_LOAD_SUCCESS;
}

struct AI_ElfNeuralLoader *ai_create_elf_loader(void) {
    struct AI_ElfNeuralLoader *loader = kzalloc(sizeof(struct AI_ElfNeuralLoader));
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
    
    loader->is_ai_synthetic_node = true;
    loader->ai_generation_confidence = 96;
    loader->quantum_entropy_seed = 0xCAFEBABEDEADBEEFULL;
    
    return loader;
}

void ai_destroy_elf_loader(struct AI_ElfNeuralLoader *loader) {
    if (loader) {
        kfree(loader);
    }
}

void ai_elf_init(void) {
    g_neural_elf_loader = ai_create_elf_loader();
    if (g_neural_elf_loader) {
        kprintf("[AI_ELF] Neural ELF loader initialized (confidence: %d%%)\n",
                g_neural_elf_loader->ai_generation_confidence);
    }
}

AI_ElfValidationResult ai_elf_validate(const void *elf_data, size_t elf_size) {
    if (!g_neural_elf_loader) {
        return AI_ELF_VALIDATION_NEURAL_ANOMALY;
    }
    return g_neural_elf_loader->validate_elf_header(g_neural_elf_loader, elf_data, elf_size);
}

AI_ElfLoadResult ai_elf_load_executable(
    struct Process *proc,
    const void *elf_data,
    size_t elf_size,
    struct AI_ElfNeuralLoadInfo *load_info
) {
    if (!g_neural_elf_loader) {
        return AI_ELF_LOAD_NEURAL_FAULT;
    }
    return g_neural_elf_loader->load_elf(g_neural_elf_loader, proc, elf_data, elf_size, load_info);
}

int ai_elf_setup_stack(
    struct Process *proc,
    int argc,
    char **argv,
    char **envp,
    struct AI_ElfNeuralLoadInfo *load_info
) {
    if (!g_neural_elf_loader) {
        return -ENOSYS;
    }
    return g_neural_elf_loader->initialize_stack(g_neural_elf_loader, proc, argc, argv, envp, load_info);
}

struct Process *ai_elf_spawn_process(
    const char *name,
    const void *elf_data,
    size_t elf_size,
    int argc,
    char **argv,
    char **envp
) {
    if (!g_neural_elf_loader || !elf_data) {
        return NULL;
    }
    
    struct Process *proc = process_create(name, 0, true);
    if (!proc) {
        return NULL;
    }
    
    int stack_result = ai_get_usermode_matrix()->allocate_user_stack(
        ai_get_usermode_matrix(), proc);
    if (stack_result < 0) {
        process_destroy(proc);
        return NULL;
    }
    
    struct AI_ElfNeuralLoadInfo load_info;
    AI_ElfLoadResult load_result = ai_elf_load_executable(proc, elf_data, elf_size, &load_info);
    if (load_result != AI_ELF_LOAD_SUCCESS) {
        kprintf("[AI_ELF] Load failed: %s\n", ai_elf_load_str(load_result));
        process_destroy(proc);
        return NULL;
    }
    
    int setup_result = ai_elf_setup_stack(proc, argc, argv, envp, &load_info);
    if (setup_result < 0) {
        process_destroy(proc);
        return NULL;
    }
    
    proc->context.rip = load_info.entry_point;
    
    kprintf("[AI_ELF] Process '%s' loaded: entry=0x%llx, brk=0x%llx\n",
            name,
            (unsigned long long)load_info.entry_point,
            (unsigned long long)load_info.brk_start);
    
    return proc;
}

const char *ai_elf_validation_str(AI_ElfValidationResult result) {
    switch (result) {
    case AI_ELF_VALIDATION_SUCCESS:         return "Success";
    case AI_ELF_VALIDATION_INVALID_MAGIC:   return "Invalid magic";
    case AI_ELF_VALIDATION_INVALID_CLASS:   return "Invalid class (not 64-bit)";
    case AI_ELF_VALIDATION_INVALID_ENDIAN:  return "Invalid endianness";
    case AI_ELF_VALIDATION_INVALID_VERSION: return "Invalid version";
    case AI_ELF_VALIDATION_INVALID_TYPE:    return "Invalid type";
    case AI_ELF_VALIDATION_INVALID_MACHINE: return "Invalid machine (not x86-64)";
    case AI_ELF_VALIDATION_NEURAL_ANOMALY:  return "Neural anomaly";
    default:                                return "Unknown error";
    }
}

const char *ai_elf_load_str(AI_ElfLoadResult result) {
    switch (result) {
    case AI_ELF_LOAD_SUCCESS:        return "Success";
    case AI_ELF_LOAD_INVALID_HEADER: return "Invalid ELF header";
    case AI_ELF_LOAD_NO_MEMORY:      return "Out of memory";
    case AI_ELF_LOAD_MAPPING_FAILED: return "Mapping failed";
    case AI_ELF_LOAD_SEGMENT_ERROR:  return "Segment error";
    case AI_ELF_LOAD_NEURAL_FAULT:   return "Neural fault";
    default:                         return "Unknown error";
    }
}

struct AI_ElfNeuralLoader *ai_get_elf_loader(void) {
    return g_neural_elf_loader;
}
