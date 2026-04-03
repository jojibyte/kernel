#ifndef _ARCH_X86_64_H
#define _ARCH_X86_64_H

#include "types.h"

#define CR0_PE          BIT(0)
#define CR0_WP          BIT(16)
#define CR0_PG          BIT(31)

#define CR4_PAE         BIT(5)
#define CR4_PGE         BIT(7)
#define CR4_OSFXSR      BIT(9)
#define CR4_OSXMMEXCPT  BIT(10)

#define RFLAGS_IF       BIT(9)
#define RFLAGS_TF       BIT(8)
#define RFLAGS_DF       BIT(10)

#define MSR_EFER            0xC0000080
#define MSR_STAR            0xC0000081
#define MSR_LSTAR           0xC0000082
#define MSR_SFMASK          0xC0000084
#define MSR_FS_BASE         0xC0000100
#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

#define EFER_SCE        BIT(0)
#define EFER_LME        BIT(8)
#define EFER_LMA        BIT(10)
#define EFER_NXE        BIT(11)

#define GDT_NULL        0x00
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

#define DPL_KERNEL      0
#define DPL_USER        3

#define INT_DIVIDE_ERROR        0
#define INT_DEBUG               1
#define INT_NMI                 2
#define INT_BREAKPOINT          3
#define INT_OVERFLOW            4
#define INT_BOUND               5
#define INT_INVALID_OPCODE      6
#define INT_DEVICE_NOT_AVAIL    7
#define INT_DOUBLE_FAULT        8
#define INT_INVALID_TSS         10
#define INT_SEGMENT_NOT_PRESENT 11
#define INT_STACK_FAULT         12
#define INT_GENERAL_PROTECTION  13
#define INT_PAGE_FAULT          14
#define INT_X87_FPU             16
#define INT_ALIGNMENT_CHECK     17
#define INT_MACHINE_CHECK       18
#define INT_SIMD_FPU            19

#define IRQ_BASE        32
#define IRQ_TIMER       (IRQ_BASE + 0)
#define IRQ_KEYBOARD    (IRQ_BASE + 1)
#define IRQ_CASCADE     (IRQ_BASE + 2)
#define IRQ_COM1        (IRQ_BASE + 4)
#define IRQ_ATA_PRIMARY (IRQ_BASE + 14)
#define IRQ_ATA_SECONDARY (IRQ_BASE + 15)

static __always_inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static __always_inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static __always_inline void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static __always_inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static __always_inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static __always_inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static __always_inline void io_wait(void) {
    outb(0x80, 0);
}

static __always_inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(val));
    return val;
}

static __always_inline void write_cr0(uint64_t val) {
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(val) : "memory");
}

static __always_inline uint64_t read_cr2(void) {
    uint64_t val;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(val));
    return val;
}

static __always_inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
    return val;
}

static __always_inline void write_cr3(uint64_t val) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(val) : "memory");
}

static __always_inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(val));
    return val;
}

static __always_inline void write_cr4(uint64_t val) {
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(val) : "memory");
}

static __always_inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static __always_inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    __asm__ __volatile__("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static __always_inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                                  uint32_t *ecx, uint32_t *edx) {
    __asm__ __volatile__("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

static __always_inline void cli(void) {
    __asm__ __volatile__("cli" ::: "memory");
}

static __always_inline void sti(void) {
    __asm__ __volatile__("sti" ::: "memory");
}

static __always_inline uint64_t read_rflags(void) {
    uint64_t flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) :: "memory");
    return flags;
}

static __always_inline bool interrupts_enabled(void) {
    return (read_rflags() & RFLAGS_IF) != 0;
}

static __always_inline uint64_t save_irq(void) {
    uint64_t flags = read_rflags();
    cli();
    return flags;
}

static __always_inline void restore_irq(uint64_t flags) {
    if (flags & RFLAGS_IF)
        sti();
}

static __always_inline void invlpg(virt_addr_t addr) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(addr) : "memory");
}

static __always_inline void flush_tlb(void) {
    write_cr3(read_cr3());
}

static __always_inline void hlt(void) {
    __asm__ __volatile__("hlt");
}

static __always_inline void cpu_pause(void) {
    __asm__ __volatile__("pause");
}

static __always_inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

struct __packed GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
};

struct __packed GdtEntryTss {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

struct __packed GdtPtr {
    uint16_t limit;
    uint64_t base;
};

struct __packed IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
};

struct __packed IdtPtr {
    uint16_t limit;
    uint64_t base;
};

struct __packed Tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
};

struct CpuRegs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

#define GDT_ACCESS_PRESENT  BIT(7)
#define GDT_ACCESS_DPL(x)   (((x) & 3) << 5)
#define GDT_ACCESS_CODE_DATA BIT(4)
#define GDT_ACCESS_EXEC     BIT(3)
#define GDT_ACCESS_RW       BIT(1)

#define GDT_FLAG_LONG       BIT(5)
#define GDT_FLAG_GRANULARITY BIT(7)

#define IDT_TYPE_INTERRUPT  0x8E
#define IDT_TYPE_TRAP       0x8F
#define IDT_TYPE_USER_INT   0xEE

void gdt_init(void);
void idt_init(void);
void tss_init(void);
void pic_init(void);
void pic_send_eoi(uint8_t irq);

#endif
