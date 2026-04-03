#include "x86_64.h"
#include "console.h"
#include "scheduler.h"
#include "process.h"

static struct GdtEntry gdt[7];
static struct GdtPtr gdt_ptr;

static struct IdtEntry idt[256];
static struct IdtPtr idt_ptr;

static struct Tss kernel_tss;

static uint8_t __aligned(16) interrupt_stack[8192];
static uint8_t __aligned(16) double_fault_stack[8192];

extern void *isr_stub_table[];

static void gdt_set_entry(int num, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags) {
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].access = access;
    gdt[num].flags_limit_high = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt[num].base_high = (base >> 24) & 0xFF;
}

static void gdt_set_tss(int num, uint64_t base, uint32_t limit) {
    struct GdtEntryTss *tss_entry = (struct GdtEntryTss *)&gdt[num];
    
    tss_entry->limit_low = limit & 0xFFFF;
    tss_entry->base_low = base & 0xFFFF;
    tss_entry->base_mid = (base >> 16) & 0xFF;
    tss_entry->access = 0x89;
    tss_entry->flags_limit_high = ((limit >> 16) & 0x0F);
    tss_entry->base_high = (base >> 24) & 0xFF;
    tss_entry->base_upper = (base >> 32) & 0xFFFFFFFF;
    tss_entry->reserved = 0;
}

void gdt_init(void) {
    gdt_set_entry(0, 0, 0, 0, 0);
    
    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_CODE_DATA | 
                  GDT_ACCESS_EXEC | GDT_ACCESS_RW,
                  GDT_FLAG_LONG | GDT_FLAG_GRANULARITY);
    
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW,
                  GDT_FLAG_GRANULARITY);
    
    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_CODE_DATA | 
                  GDT_ACCESS_RW | GDT_ACCESS_DPL(3),
                  GDT_FLAG_GRANULARITY);
    
    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_CODE_DATA | 
                  GDT_ACCESS_EXEC | GDT_ACCESS_RW | GDT_ACCESS_DPL(3),
                  GDT_FLAG_LONG | GDT_FLAG_GRANULARITY);
    
    gdt_set_tss(5, (uint64_t)&kernel_tss, sizeof(kernel_tss) - 1);
    
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    
    __asm__ __volatile__(
        "lgdt %0\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "m"(gdt_ptr) : "rax", "memory"
    );
}

static void idt_set_entry(int num, uint64_t handler, uint8_t type, uint8_t ist) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = GDT_KERNEL_CODE;
    idt[num].ist = ist;
    idt[num].type_attr = type;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t ist = 0;
        
        if (i == INT_DOUBLE_FAULT) {
            ist = 1;
        }
        
        idt_set_entry(i, (uint64_t)isr_stub_table[i], IDT_TYPE_INTERRUPT, ist);
    }
    
    idt_set_entry(0x80, (uint64_t)isr_stub_table[0x80], IDT_TYPE_USER_INT, 0);
    
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;
    
    __asm__ __volatile__("lidt %0" : : "m"(idt_ptr));
}

void tss_init(void) {
    uint8_t *tss_ptr = (uint8_t *)&kernel_tss;
    for (size_t i = 0; i < sizeof(kernel_tss); i++) {
        tss_ptr[i] = 0;
    }
    
    kernel_tss.rsp0 = (uint64_t)&interrupt_stack[sizeof(interrupt_stack)];
    kernel_tss.ist1 = (uint64_t)&double_fault_stack[sizeof(double_fault_stack)];
    kernel_tss.iopb_offset = sizeof(kernel_tss);
    
    __asm__ __volatile__("ltr %%ax" : : "a"(GDT_TSS));
}

void pic_init(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    io_wait();
    
    outb(0x21, IRQ_BASE);
    outb(0xA1, IRQ_BASE + 8);
    io_wait();
    
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    io_wait();
    
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();
    
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static const char *exception_names[] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception"
};

extern void timer_handler(void);
extern void keyboard_irq_handler(void);

void interrupt_handler(struct CpuRegs *regs) {
    uint64_t int_no = regs->int_no;
    
    if (int_no < 32) {
        const char *name = (int_no < 22) ? exception_names[int_no] : "Unknown Exception";
        
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
        kprintf("\n!!! CPU EXCEPTION: %s (#%llu) !!!\n", name, (unsigned long long)int_no);
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        
        kprintf("Error code: 0x%llx\n", (unsigned long long)regs->err_code);
        kprintf("RIP: 0x%llx  CS: 0x%llx\n", (unsigned long long)regs->rip, (unsigned long long)regs->cs);
        kprintf("RFLAGS: 0x%llx\n", (unsigned long long)regs->rflags);
        kprintf("RSP: 0x%llx  SS: 0x%llx\n", (unsigned long long)regs->rsp, (unsigned long long)regs->ss);
        kprintf("RAX: 0x%llx  RBX: 0x%llx\n", (unsigned long long)regs->rax, (unsigned long long)regs->rbx);
        kprintf("RCX: 0x%llx  RDX: 0x%llx\n", (unsigned long long)regs->rcx, (unsigned long long)regs->rdx);
        kprintf("RSI: 0x%llx  RDI: 0x%llx\n", (unsigned long long)regs->rsi, (unsigned long long)regs->rdi);
        kprintf("RBP: 0x%llx\n", (unsigned long long)regs->rbp);
        
        if (int_no == INT_PAGE_FAULT) {
            uint64_t cr2 = read_cr2();
            kprintf("CR2 (fault addr): 0x%llx\n", (unsigned long long)cr2);
            kprintf("Fault type: %s%s%s%s\n",
                    (regs->err_code & 1) ? "Protection " : "Not-present ",
                    (regs->err_code & 2) ? "Write " : "Read ",
                    (regs->err_code & 4) ? "User " : "Supervisor ",
                    (regs->err_code & 8) ? "Reserved-bit " : "");
        }
        
        kprintf("\nSystem halted.\n");
        cli();
        for (;;) hlt();
    }
    
    if (int_no >= IRQ_BASE && int_no < IRQ_BASE + 16) {
        uint8_t irq = int_no - IRQ_BASE;
        
        switch (irq) {
        case 0:
            timer_handler();
            break;
        case 1:
            keyboard_irq_handler();
            break;
        }
        
        pic_send_eoi(irq);
        return;
    }
    
    if (int_no == 0x80) {
        return;
    }
    
    kprintf("Unknown interrupt: %llu\n", (unsigned long long)int_no);
}

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    
    uint64_t star = ((uint64_t)(GDT_USER_DATA - 8) << 48) | 
                    ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(MSR_STAR, star);
    
    extern void syscall_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    wrmsr(MSR_SFMASK, RFLAGS_IF | RFLAGS_TF | RFLAGS_DF);
}
