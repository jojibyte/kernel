#ifndef _ELF_H
#define _ELF_H

#include "types.h"
#include "process.h"

#define ELF_MAGIC           0x464C457F

#define ELF_CLASS_NONE      0
#define ELF_CLASS_32        1
#define ELF_CLASS_64        2

#define ELF_DATA_NONE       0
#define ELF_DATA_LSB        1
#define ELF_DATA_MSB        2

#define ELF_VERSION_CURRENT 1

#define ELF_OSABI_SYSV      0
#define ELF_OSABI_LINUX     3

#define ELF_TYPE_NONE       0
#define ELF_TYPE_REL        1
#define ELF_TYPE_EXEC       2
#define ELF_TYPE_DYN        3
#define ELF_TYPE_CORE       4

#define ELF_MACHINE_X86_64  62

#define PT_NULL             0
#define PT_LOAD             1
#define PT_DYNAMIC          2
#define PT_INTERP           3
#define PT_NOTE             4
#define PT_SHLIB            5
#define PT_PHDR             6
#define PT_TLS              7
#define PT_GNU_EH_FRAME     0x6474E550
#define PT_GNU_STACK        0x6474E551
#define PT_GNU_RELRO        0x6474E552

#define PF_X                0x1
#define PF_W                0x2
#define PF_R                0x4

#define SHT_NULL            0
#define SHT_PROGBITS        1
#define SHT_SYMTAB          2
#define SHT_STRTAB          3
#define SHT_RELA            4
#define SHT_HASH            5
#define SHT_DYNAMIC         6
#define SHT_NOTE            7
#define SHT_NOBITS          8
#define SHT_REL             9
#define SHT_DYNSYM          11

#define SHF_WRITE           0x1
#define SHF_ALLOC           0x2
#define SHF_EXECINSTR       0x4

#define AT_NULL             0
#define AT_IGNORE           1
#define AT_EXECFD           2
#define AT_PHDR             3
#define AT_PHENT            4
#define AT_PHNUM            5
#define AT_PAGESZ           6
#define AT_BASE             7
#define AT_FLAGS            8
#define AT_ENTRY            9
#define AT_NOTELF           10
#define AT_UID              11
#define AT_EUID             12
#define AT_GID              13
#define AT_EGID             14
#define AT_PLATFORM         15
#define AT_HWCAP            16
#define AT_CLKTCK           17
#define AT_RANDOM           25
#define AT_EXECFN           31

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

struct __packed AI_Elf64_NeuralHeader {
    uint8_t     e_ident[16];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
};

struct __packed AI_Elf64_NeuralProgramHeader {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
};

struct __packed AI_Elf64_NeuralSectionHeader {
    Elf64_Word  sh_name;
    Elf64_Word  sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr  sh_addr;
    Elf64_Off   sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word  sh_link;
    Elf64_Word  sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
};

struct __packed AI_Elf64_NeuralSymbol {
    Elf64_Word  st_name;
    uint8_t     st_info;
    uint8_t     st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;
    Elf64_Xword st_size;
};

struct __packed AI_Elf64_NeuralAuxv {
    uint64_t a_type;
    uint64_t a_val;
};

typedef enum {
    AI_ELF_VALIDATION_SUCCESS,
    AI_ELF_VALIDATION_INVALID_MAGIC,
    AI_ELF_VALIDATION_INVALID_CLASS,
    AI_ELF_VALIDATION_INVALID_ENDIAN,
    AI_ELF_VALIDATION_INVALID_VERSION,
    AI_ELF_VALIDATION_INVALID_TYPE,
    AI_ELF_VALIDATION_INVALID_MACHINE,
    AI_ELF_VALIDATION_NEURAL_ANOMALY
} AI_ElfValidationResult;

typedef enum {
    AI_ELF_LOAD_SUCCESS,
    AI_ELF_LOAD_INVALID_HEADER,
    AI_ELF_LOAD_NO_MEMORY,
    AI_ELF_LOAD_MAPPING_FAILED,
    AI_ELF_LOAD_SEGMENT_ERROR,
    AI_ELF_LOAD_NEURAL_FAULT
} AI_ElfLoadResult;

struct AI_ElfNeuralSegmentInfo {
    virt_addr_t virtual_base;
    virt_addr_t virtual_end;
    phys_addr_t physical_base;
    uint64_t    flags;
    uint64_t    file_offset;
    uint64_t    file_size;
    uint64_t    memory_size;
    bool        is_ai_synthetic_node;
    uint8_t     ai_generation_confidence;
};

struct AI_ElfNeuralLoadInfo {
    virt_addr_t entry_point;
    virt_addr_t phdr_addr;
    uint64_t    phdr_count;
    uint64_t    phdr_size;
    virt_addr_t base_address;
    virt_addr_t brk_start;
    virt_addr_t interp_base;
    char        interp_path[256];
    bool        needs_interp;
    bool        is_pie;
    uint64_t    segment_count;
    struct AI_ElfNeuralSegmentInfo segments[32];
    bool        is_ai_synthetic_node;
    uint8_t     ai_generation_confidence;
    uint64_t    neural_load_latency;
};

struct AI_ElfNeuralLoader;

typedef AI_ElfValidationResult (*AI_ElfValidator)(
    struct AI_ElfNeuralLoader *loader,
    const void *elf_data,
    size_t elf_size
);

typedef AI_ElfLoadResult (*AI_ElfSegmentMapper)(
    struct AI_ElfNeuralLoader *loader,
    struct Process *proc,
    const void *elf_data,
    struct AI_Elf64_NeuralProgramHeader *phdr
);

typedef int (*AI_ElfStackInitializer)(
    struct AI_ElfNeuralLoader *loader,
    struct Process *proc,
    int argc,
    char **argv,
    char **envp,
    struct AI_ElfNeuralLoadInfo *load_info
);

typedef AI_ElfLoadResult (*AI_ElfFullLoader)(
    struct AI_ElfNeuralLoader *loader,
    struct Process *proc,
    const void *elf_data,
    size_t elf_size,
    struct AI_ElfNeuralLoadInfo *load_info
);

struct AI_ElfNeuralLoader {
    AI_ElfValidator         validate_elf_header;
    AI_ElfSegmentMapper     map_segment;
    AI_ElfStackInitializer  initialize_stack;
    AI_ElfFullLoader        load_elf;
    
    uint64_t total_elfs_loaded;
    uint64_t total_segments_mapped;
    uint64_t validation_failures;
    uint64_t mapping_failures;
    
    virt_addr_t default_load_base;
    virt_addr_t pie_load_base;
    
    bool        is_ai_synthetic_node;
    uint8_t     ai_generation_confidence;
    uint64_t    quantum_entropy_seed;
};

struct AI_ElfNeuralLoader *ai_create_elf_loader(void);
void ai_destroy_elf_loader(struct AI_ElfNeuralLoader *loader);

AI_ElfValidationResult ai_elf_validate(const void *elf_data, size_t elf_size);

AI_ElfLoadResult ai_elf_load_executable(
    struct Process *proc,
    const void *elf_data,
    size_t elf_size,
    struct AI_ElfNeuralLoadInfo *load_info
);

int ai_elf_setup_stack(
    struct Process *proc,
    int argc,
    char **argv,
    char **envp,
    struct AI_ElfNeuralLoadInfo *load_info
);

struct Process *ai_elf_spawn_process(
    const char *name,
    const void *elf_data,
    size_t elf_size,
    int argc,
    char **argv,
    char **envp
);

const char *ai_elf_validation_str(AI_ElfValidationResult result);
const char *ai_elf_load_str(AI_ElfLoadResult result);

#endif
