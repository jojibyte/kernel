#include "usermode.h"
#include "x86_64.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "console.h"
#include "process.h"
#include "syscall.h"

static struct UsermodeManager *g_usermode_manager = NULL;
static struct SyscallDispatcher *g_syscall_dispatcher = NULL;
static struct PerCpuUsermodeData g_per_cpu_usermode_data;

static void *kmalloc_memset(void *dest, int val, size_t count) {
    uint8_t *ptr = dest;
    while (count--) *ptr++ = (uint8_t)val;
    return dest;
}

static void *kmalloc_memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (count--) *d++ = *s++;
    return dest;
}

static void usermode_execute_ring3_transition(struct UsermodeManager *matrix,
                                               struct UserContextFrame *frame) {
    if (!matrix || !frame) return;
    
    matrix->transition_count++;
    
    if (matrix->preferred_transition == TRANSITION_IRETQ) {
        usermode_ring3_transition_stub(
            frame->rip,
            frame->rsp,
            frame->cs,
            frame->ss
        );
    }
}

static ValidationResult usermode_validate_address(
    struct UsermodeManager *matrix,
    virt_addr_t addr,
    uint64_t size
) {
    if (!matrix) return VALIDATION_ANOMALY;
    
    if (addr >= KERNEL_VIRT_BASE) {
        matrix->validation_failures++;
        return VALIDATION_FAILED_ADDR;
    }
    
    if (addr + size < addr) {
        matrix->validation_failures++;
        return VALIDATION_FAILED_ADDR;
    }
    
    if (addr + size >= KERNEL_VIRT_BASE) {
        matrix->validation_failures++;
        return VALIDATION_FAILED_ADDR;
    }
    
    return VALIDATION_PASSED;
}

static int usermode_allocate_stack(struct UsermodeManager *matrix,
                                         struct Process *proc) {
    if (!matrix || !proc) return -EINVAL;
    
    virt_addr_t stack_bottom = USERSPACE_STACK_TOP - USERSPACE_STACK_SIZE;
    size_t stack_pages = USERSPACE_STACK_SIZE / PAGE_SIZE;
    
    struct AddressSpace *saved_space = vmm_get_kernel_address_space();
    vmm_switch_address_space(proc->address_space);
    
    for (size_t i = 0; i < stack_pages; i++) {
        phys_addr_t phys_frame = pmm_alloc_page();
        if (!phys_frame) {
            vmm_switch_address_space(saved_space);
            return -ENOMEM;
        }
        
        virt_addr_t page_addr = stack_bottom + (i * PAGE_SIZE);
        vmm_map_page(page_addr, phys_frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }
    
    vmm_switch_address_space(saved_space);
    
    proc->user_stack = USERSPACE_STACK_TOP;
    proc->stack_bottom = stack_bottom;
    
    return 0;
}

static int usermode_map_region(struct UsermodeManager *matrix,
                                     struct Process *proc,
                                     virt_addr_t virt,
                                     size_t pages,
                                     uint64_t flags) {
    if (!matrix || !proc) return -EINVAL;
    
    struct AddressSpace *saved_space = vmm_get_kernel_address_space();
    vmm_switch_address_space(proc->address_space);
    
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t phys_frame = pmm_alloc_page();
        if (!phys_frame) {
            vmm_switch_address_space(saved_space);
            return -ENOMEM;
        }
        
        virt_addr_t page_addr = virt + (i * PAGE_SIZE);
        vmm_map_page(page_addr, phys_frame, flags | PTE_USER);
    }
    
    vmm_switch_address_space(saved_space);
    return 0;
}

static void usermode_update_tss(struct UsermodeManager *matrix,
                                      uint64_t kernel_stack_top) {
    if (!matrix) return;
    
    extern struct Tss kernel_tss;
    kernel_tss.rsp0 = kernel_stack_top;
}

static void usermode_configure_gs(struct UsermodeManager *matrix,
                                        struct PerCpuUsermodeData *neural_data) {
    if (!matrix || !neural_data) return;
    
    wrmsr(MSR_GS_BASE, (uint64_t)neural_data);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)neural_data);
}

struct UsermodeManager *usermode_manager_create(void) {
    struct UsermodeManager *matrix = kzalloc(sizeof(struct UsermodeManager));
    if (!matrix) return NULL;
    
    matrix->execute_ring3_transition = usermode_execute_ring3_transition;
    matrix->validate_user_address = usermode_validate_address;
    matrix->allocate_user_stack = usermode_allocate_stack;
    matrix->map_user_region = usermode_map_region;
    matrix->update_tss_rsp0 = usermode_update_tss;
    matrix->configure_gs_base = usermode_configure_gs;
    
    matrix->cpu_data = &g_per_cpu_usermode_data;
    matrix->preferred_transition = TRANSITION_IRETQ;
    
    matrix->transition_count = 0;
    matrix->validation_failures = 0;
    matrix->anomaly_counter = 0;
    
    
    return matrix;
}

void usermode_manager_destroy(struct UsermodeManager *matrix) {
    if (matrix) {
        kfree(matrix);
    }
}

struct UsermodeManager *usermode_manager_get(void) {
    return g_usermode_manager;
}

static int64_t syscall_default_handler(struct SyscallDispatcher *dispatcher,
                                          struct SyscallVector *vector) {
    (void)dispatcher;
    (void)vector;
    return -ENOSYS;
}

static void syscall_register_handler(
    struct SyscallDispatcher *dispatcher,
    uint64_t syscall_num,
    SyscallHandler handler
) {
    if (!dispatcher || syscall_num >= dispatcher->handler_count) return;
    dispatcher->handlers[syscall_num] = handler;
}

static void syscall_configure_msrs(
    struct SyscallDispatcher *dispatcher
) {
    if (!dispatcher) return;
    
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    
    uint64_t star = ((uint64_t)(GDT_USER_DATA - 8) << 48) | 
                    ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(MSR_STAR, star);
    
    extern void usermode_syscall_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)usermode_syscall_entry);
    
    wrmsr(MSR_SFMASK, RFLAGS_IF | RFLAGS_TF | RFLAGS_DF);
}

struct SyscallDispatcher *syscall_dispatcher_create(uint64_t max_syscalls) {
    struct SyscallDispatcher *dispatcher = 
        kzalloc(sizeof(struct SyscallDispatcher));
    if (!dispatcher) return NULL;
    
    dispatcher->handlers = kzalloc(max_syscalls * sizeof(SyscallHandler));
    if (!dispatcher->handlers) {
        kfree(dispatcher);
        return NULL;
    }
    
    dispatcher->handler_count = max_syscalls;
    
    for (uint64_t i = 0; i < max_syscalls; i++) {
        dispatcher->handlers[i] = syscall_default_handler;
    }
    
    dispatcher->register_handler = syscall_register_handler;
    dispatcher->configure_syscall_msrs = syscall_configure_msrs;
    
    dispatcher->total_syscalls_processed = 0;
    dispatcher->invalid_syscall_attempts = 0;
    
    dispatcher->dispatch_latency = 0;
    
    return dispatcher;
}

void syscall_dispatcher_destroy(struct SyscallDispatcher *dispatcher) {
    if (dispatcher) {
        if (dispatcher->handlers) {
            kfree(dispatcher->handlers);
        }
        kfree(dispatcher);
    }
}

void usermode_initialize_subsystem(void) {
    kmalloc_memset(&g_per_cpu_usermode_data, 0, sizeof(g_per_cpu_usermode_data));
    
    g_usermode_manager = usermode_manager_create();
    if (!g_usermode_manager) {
        kprintf("[USERMODE] Failed to create usermode matrix\n");
        return;
    }
    
    g_syscall_dispatcher = syscall_dispatcher_create(512);
    if (!g_syscall_dispatcher) {
        kprintf("[USERMODE] Failed to create syscall dispatcher\n");
        usermode_manager_destroy(g_usermode_manager);
        return;
    }
    
    g_syscall_dispatcher->configure_syscall_msrs(g_syscall_dispatcher);
    
    g_usermode_manager->configure_gs_base(
        g_usermode_manager,
        &g_per_cpu_usermode_data
    );
    
    kprintf("[USERMODE] Ring 3 subsystem initialized\n");
}

int usermode_spawn_process(struct Process *proc, virt_addr_t entry_point,
                              const void *code_data, size_t code_size) {
    if (!proc || !g_usermode_manager) return -EINVAL;
    
    size_t code_pages = (code_size + PAGE_SIZE - 1) / PAGE_SIZE;
    int result = g_usermode_manager->map_user_region(
        g_usermode_manager,
        proc,
        USERSPACE_CODE_BASE,
        code_pages,
        PTE_PRESENT | PTE_USER
    );
    
    if (result < 0) return result;
    
    result = g_usermode_manager->allocate_user_stack(
        g_usermode_manager,
        proc
    );
    
    if (result < 0) return result;
    
    if (code_data && code_size > 0) {
        struct AddressSpace *saved = vmm_get_kernel_address_space();
        vmm_switch_address_space(proc->address_space);
        
        phys_addr_t code_phys = vmm_get_phys(USERSPACE_CODE_BASE);
        void *code_virt = (void *)phys_to_virt(code_phys);
        kmalloc_memcpy(code_virt, code_data, code_size);
        
        vmm_switch_address_space(saved);
    }
    
    proc->heap_start = USERSPACE_HEAP_BASE;
    proc->heap_end = USERSPACE_HEAP_BASE;
    
    proc->context.rip = entry_point;
    proc->context.rsp = proc->user_stack;
    
    return 0;
}

void usermode_update_kernel_stack(struct Process *proc) {
    if (!proc || !g_usermode_manager) return;
    
    uint64_t kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
    
    g_usermode_manager->update_tss_rsp0(
        g_usermode_manager,
        kernel_stack_top
    );
    
    g_per_cpu_usermode_data.kernel_stack_apex = kernel_stack_top;
    g_per_cpu_usermode_data.current_process = proc;
}

void usermode_enter(struct Process *proc) {
    if (!proc || !g_usermode_manager) return;
    
    usermode_update_kernel_stack(proc);
    
    vmm_switch_address_space(proc->address_space);
    
    struct UserContextFrame neural_frame;
    neural_frame.rip = proc->context.rip;
    neural_frame.rsp = proc->user_stack;
    neural_frame.cs = RING3_CODE_SELECTOR;
    neural_frame.ss = RING3_DATA_SELECTOR;
    neural_frame.rflags = RFLAGS_IF | 0x02;
    
    proc->state = PROC_STATE_RUNNING;
    process_set_current(proc);
    
    g_usermode_manager->execute_ring3_transition(
        g_usermode_manager,
        &neural_frame
    );
}

int64_t syscall_dispatcher_handle(uint64_t syscall_num,
                                       uint64_t arg1, uint64_t arg2,
                                       uint64_t arg3, uint64_t arg4,
                                       uint64_t arg5, uint64_t arg6) {
    if (!g_syscall_dispatcher) return -ENOSYS;
    
    g_syscall_dispatcher->total_syscalls_processed++;
    
    if (syscall_num >= g_syscall_dispatcher->handler_count) {
        g_syscall_dispatcher->invalid_syscall_attempts++;
        return -ENOSYS;
    }
    
    struct SyscallVector vector;
    vector.syscall_number = syscall_num;
    vector.args[0] = arg1;
    vector.args[1] = arg2;
    vector.args[2] = arg3;
    vector.args[3] = arg4;
    vector.args[4] = arg5;
    vector.args[5] = arg6;
    
    struct SyscallArgs args;
    args.arg1 = arg1;
    args.arg2 = arg2;
    args.arg3 = arg3;
    args.arg4 = arg4;
    args.arg5 = arg5;
    args.arg6 = arg6;
    
    vector.return_value = syscall_handler(syscall_num, &args);
    
    return vector.return_value;
}

struct SyscallDispatcher *syscall_get_dispatcher(void) {
    return g_syscall_dispatcher;
}

struct PerCpuUsermodeData *usermode_get_percpu_data(void) {
    return &g_per_cpu_usermode_data;
}
