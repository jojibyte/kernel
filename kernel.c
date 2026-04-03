#include "types.h"
#include "x86_64.h"
#include "multiboot2.h"
#include "console.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "process.h"
#include "scheduler.h"
#include "vfs.h"
#include "net.h"

struct BootInfo boot_info;

extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

static void parse_multiboot(struct Multiboot2Info *mbi);
static void print_boot_info(void);
void init_process(void *arg);

void __noreturn kernel_main(uint32_t mb_info_addr, uint32_t mb_magic) {
    console_init();
    console_clear();
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("kernel\n");
    kprintf("======\n\n");
    console_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    if (mb_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        kprintf("ERROR: Invalid multiboot2 magic: 0x%x\n", mb_magic);
        kprintf("Expected: 0x%x\n", MULTIBOOT2_BOOTLOADER_MAGIC);
        goto halt;
    }

    kprintf("[BOOT] Parsing multiboot2 info at 0x%x\n", mb_info_addr);
    parse_multiboot((struct Multiboot2Info *)(uint64_t)mb_info_addr);
    print_boot_info();

    kprintf("[CPU]  Initializing GDT... ");
    gdt_init();
    kprintf("OK\n");

    kprintf("[CPU]  Initializing IDT... ");
    idt_init();
    kprintf("OK\n");

    kprintf("[CPU]  Initializing PIC... ");
    pic_init();
    kprintf("OK\n");

    kprintf("[CPU]  Initializing TSS... ");
    tss_init();
    kprintf("OK\n");

    kprintf("[MEM]  Initializing PMM... ");
    pmm_init();
    kprintf("OK (%u MB available)\n", 
            (uint32_t)(pmm_get_free_memory() / MB));

    kprintf("[MEM]  Initializing VMM... ");
    vmm_init();
    kprintf("OK\n");

    kprintf("[MEM]  Initializing heap... ");
    heap_init();
    kprintf("OK\n");

    kprintf("[DRV]  Initializing PIT timer... ");
    timer_init(1000);
    kprintf("OK\n");

    kprintf("[DRV]  Initializing keyboard... ");
    keyboard_init();
    kprintf("OK\n");

    kprintf("[PROC] Initializing scheduler... ");
    scheduler_init();
    kprintf("OK\n");

    kprintf("[FS]   Initializing VFS... ");
    vfs_init();
    kprintf("OK\n");

    kprintf("[NET]  Initializing network... ");
    net_init();
    kprintf("OK\n");

    kprintf("\n[KERN] Enabling interrupts...\n");
    sti();

    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintf("\n=== Kernel initialization complete ===\n\n");
    console_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    kprintf("[PROC] Creating init process...\n");
    process_create_kernel_thread("init", init_process, NULL);

    kprintf("[PROC] Starting scheduler...\n\n");
    scheduler_start();

halt:
    kprintf("\nKernel halted.\n");
    for (;;) {
        hlt();
    }
}

static void parse_multiboot(struct Multiboot2Info *mbi) {
    struct Multiboot2Tag *tag;

    boot_info.kernel_start = (uint64_t)__kernel_start;
    boot_info.kernel_end = (uint64_t)__kernel_end;
    boot_info.cmdline = NULL;
    boot_info.total_memory = 0;
    boot_info.has_framebuffer = false;
    boot_info.acpi_rsdp = NULL;
    boot_info.mmap_entries = NULL;
    boot_info.mmap_entry_count = 0;

    for (tag = (struct Multiboot2Tag *)((uint8_t *)mbi + 8);
         tag->type != MULTIBOOT2_INFO_END;
         tag = (struct Multiboot2Tag *)((uint8_t *)tag + ALIGN_UP(tag->size, 8))) {
        
        switch (tag->type) {
        case MULTIBOOT2_INFO_CMDLINE: {
            struct Multiboot2TagString *cmdline = (void *)tag;
            boot_info.cmdline = cmdline->string;
            break;
        }

        case MULTIBOOT2_INFO_BASIC_MEMINFO: {
            struct Multiboot2TagBasicMeminfo *mem = (void *)tag;
            boot_info.mem_lower = mem->mem_lower;
            boot_info.mem_upper = mem->mem_upper;
            break;
        }

        case MULTIBOOT2_INFO_MMAP: {
            struct Multiboot2TagMmap *mmap = (void *)tag;
            boot_info.mmap_entries = mmap->entries;
            boot_info.mmap_entry_size = mmap->entry_size;
            boot_info.mmap_entry_count = 
                (tag->size - sizeof(struct Multiboot2TagMmap)) / mmap->entry_size;
            
            for (uint32_t i = 0; i < boot_info.mmap_entry_count; i++) {
                struct Multiboot2MmapEntry *entry = 
                    (void *)((uint8_t *)mmap->entries + i * mmap->entry_size);
                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    boot_info.total_memory += entry->len;
                }
            }
            break;
        }

        case MULTIBOOT2_INFO_FRAMEBUFFER: {
            struct Multiboot2TagFramebufferCommon *fb = (void *)tag;
            boot_info.has_framebuffer = true;
            boot_info.fb_addr = fb->framebuffer_addr;
            boot_info.fb_pitch = fb->framebuffer_pitch;
            boot_info.fb_width = fb->framebuffer_width;
            boot_info.fb_height = fb->framebuffer_height;
            boot_info.fb_bpp = fb->framebuffer_bpp;
            break;
        }

        case MULTIBOOT2_INFO_ACPI_OLD:
        case MULTIBOOT2_INFO_ACPI_NEW: {
            boot_info.acpi_rsdp = (void *)((uint8_t *)tag + 8);
            break;
        }
        }
    }
}

static void print_boot_info(void) {
    kprintf("[BOOT] Kernel: 0x%llx - 0x%llx (%u KB)\n",
            (unsigned long long)boot_info.kernel_start, 
            (unsigned long long)boot_info.kernel_end,
            (uint32_t)((boot_info.kernel_end - boot_info.kernel_start) / KB));
    
    kprintf("[BOOT] Total memory: %u MB\n", 
            (uint32_t)(boot_info.total_memory / MB));
    
    if (boot_info.cmdline) {
        kprintf("[BOOT] Command line: %s\n", boot_info.cmdline);
    }
    
    if (boot_info.has_framebuffer) {
        kprintf("[BOOT] Framebuffer: %ux%u @ 0x%llx\n",
                boot_info.fb_width, boot_info.fb_height, 
                (unsigned long long)boot_info.fb_addr);
    }
    
    kprintf("\n");
}

void init_process(void *arg) {
    (void)arg;
    
    kprintf("[INIT] Init process started (PID %d)\n", process_current()->pid);
    
    kprintf("[INIT] Mounting root filesystem...\n");
    vfs_mount("/", "ramfs", NULL);
    
    vfs_mkdir("/dev", 0755);
    vfs_mkdir("/proc", 0755);
    vfs_mkdir("/tmp", 0755);
    vfs_mkdir("/home", 0755);
    
    kprintf("[INIT] Creating device nodes...\n");
    
    kprintf("[INIT] Starting shell...\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("Welcome to kernel\n");
    kprintf("Type 'help' for available commands.\n\n");
    
    shell_run();
}

void __noreturn panic(const char *fmt, ...) {
    cli();
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    kprintf("\n!!! KERNEL PANIC !!!\n\n");
    
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
    
    kprintf("\n\nSystem halted.\n");
    
    uint64_t rsp, rbp;
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp));
    __asm__ __volatile__("mov %%rbp, %0" : "=r"(rbp));
    
    kprintf("\nRSP: 0x%llx  RBP: 0x%llx\n", 
            (unsigned long long)rsp, (unsigned long long)rbp);
    
    kprintf("\nStack trace:\n");
    uint64_t *frame = (uint64_t *)rbp;
    for (int i = 0; i < 10 && frame; i++) {
        uint64_t ret_addr = frame[1];
        if (ret_addr == 0)
            break;
        kprintf("  [%d] 0x%llx\n", i, (unsigned long long)ret_addr);
        frame = (uint64_t *)frame[0];
    }
    
    for (;;) {
        hlt();
    }
}
