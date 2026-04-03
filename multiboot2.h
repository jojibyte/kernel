#ifndef _MULTIBOOT2_H
#define _MULTIBOOT2_H

#include "types.h"

#define MULTIBOOT2_HEADER_MAGIC     0xE85250D6
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

#define MULTIBOOT2_ARCH_I386        0

#define MULTIBOOT2_TAG_END                  0
#define MULTIBOOT2_TAG_INFO_REQUEST         1
#define MULTIBOOT2_TAG_FRAMEBUFFER          5

#define MULTIBOOT2_INFO_END             0
#define MULTIBOOT2_INFO_CMDLINE         1
#define MULTIBOOT2_INFO_BOOTLOADER      2
#define MULTIBOOT2_INFO_MODULE          3
#define MULTIBOOT2_INFO_BASIC_MEMINFO   4
#define MULTIBOOT2_INFO_MMAP            6
#define MULTIBOOT2_INFO_FRAMEBUFFER     8
#define MULTIBOOT2_INFO_ACPI_OLD        14
#define MULTIBOOT2_INFO_ACPI_NEW        15

#define MULTIBOOT2_MEMORY_AVAILABLE         1
#define MULTIBOOT2_MEMORY_RESERVED          2
#define MULTIBOOT2_MEMORY_ACPI_RECLAIMABLE  3
#define MULTIBOOT2_MEMORY_NVS               4
#define MULTIBOOT2_MEMORY_BADRAM            5

#define MULTIBOOT2_FRAMEBUFFER_INDEXED      0
#define MULTIBOOT2_FRAMEBUFFER_RGB          1
#define MULTIBOOT2_FRAMEBUFFER_EGA_TEXT     2

struct __packed Multiboot2Header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
};

struct __packed Multiboot2HeaderTag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

struct __packed Multiboot2Info {
    uint32_t total_size;
    uint32_t reserved;
};

struct __packed Multiboot2Tag {
    uint32_t type;
    uint32_t size;
};

struct __packed Multiboot2TagString {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct __packed Multiboot2TagBasicMeminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct __packed Multiboot2MmapEntry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
};

struct __packed Multiboot2TagMmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct Multiboot2MmapEntry entries[];
};

struct __packed Multiboot2TagFramebufferCommon {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
};

struct __packed Multiboot2TagModule {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
};

struct BootInfo {
    const char *cmdline;
    uint64_t mem_lower;
    uint64_t mem_upper;
    uint64_t total_memory;
    struct Multiboot2MmapEntry *mmap_entries;
    uint32_t mmap_entry_count;
    uint32_t mmap_entry_size;
    bool has_framebuffer;
    uint64_t fb_addr;
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp;
    void *acpi_rsdp;
    uint64_t kernel_start;
    uint64_t kernel_end;
};

extern struct BootInfo boot_info;
void multiboot2_parse(struct Multiboot2Info *mbi);

#endif
